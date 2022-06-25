import {
	createConnection,
	TextDocuments,
	Diagnostic,
	DiagnosticSeverity,
	ProposedFeatures,
	InitializeParams,
	DidChangeConfigurationNotification,
	TextDocumentSyncKind,
	InitializeResult,
	CodeAction,
	Command,
	CodeActionKind,
	TextDocumentEdit,
	TextEdit,
	WorkspaceFolder
} from 'vscode-languageserver/node';

import {
	TextDocument
} from 'vscode-languageserver-textdocument';

import { exec, execSync } from 'child_process';
import { promisify } from 'util';
import { readFile } from 'fs';
import { privateEncrypt } from 'crypto';
// Create a connection for the server, using Node's IPC as a transport.
// Also include all preview / proposed LSP features.
const connection = createConnection(ProposedFeatures.all);

// Create a simple text document manager.
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;
let messDef:string;

let startedSA4U_Z3 = false;
connection.onInitialize((params: InitializeParams) => {

	const capabilities = params.capabilities;

	// Does the client support the `workspace/configuration` request?
	// If not, we fall back using global settings.
	hasConfigurationCapability = !!(
		capabilities.workspace && !!capabilities.workspace.configuration
	);
	hasWorkspaceFolderCapability = !!(
		capabilities.workspace && !!capabilities.workspace.workspaceFolders
	);

	const result: InitializeResult = {
		capabilities: {
			codeActionProvider: true,
			textDocumentSync: TextDocumentSyncKind.Incremental,
			executeCommandProvider: {
				commands: ['sa4u.fix']
			}
		}
	};
	if (hasWorkspaceFolderCapability) {
		result.capabilities.workspace = {
			workspaceFolders: {
				supported: true
			}
		};
	}
	return result;
});

connection.onInitialized(() => {
	
	if (hasConfigurationCapability) {
		// Register for all configuration changes.
		connection.client.register(DidChangeConfigurationNotification.type, undefined);
		connection.workspace.getConfiguration('SA4U').then((value) => {
			if (value.messageDefinition) {
				messDef = value.messageDefinition;
			}
		});
	}
	if (hasWorkspaceFolderCapability) {
		connection.workspace.onDidChangeWorkspaceFolders(_event => {
			connection.console.log('Workspace folder change event received.');
		});
	}
	connection.workspace.getConfiguration();
	connection.workspace.getWorkspaceFolders().then((folders:null|WorkspaceFolder[]|void) => {
		if (folders) {
			folders.forEach((folder) => {
				dockerContainer(folder);
			});
		}
	});
	documents.all().forEach((doc)=>validateTextDocument(doc));
});

class SA4UConfig {
	compilationDir: string
	constructor(compilationDir: string) {
		this.compilationDir = '/src/' + compilationDir;
	}
}

// TODO: it would be nice to automatically discover the compile_commands.json file.
const readFileAsync = promisify(readFile);

async function getSA4UConfig(projectRootDir: string) {
	try {
		const configJSON = JSON.parse((await readFileAsync(projectRootDir + "/" + ".sa4u.json")).toString());
		return new SA4UConfig(configJSON['CompilationDir']);
	} catch (err) {
		console.warn('unable to read SA4U config: ' + err);
		return new SA4UConfig('');
	}
}

async function dockerContainer(folder: WorkspaceFolder) {
	let path = decodeURIComponent(folder.uri);
	const filePath = path;
	path = path.replace(/(^\w+:|^)\/\//, '');
	path = path.replace(/:/, '');
	const diagnosticsMap = new Map<string, Diagnostic[]>();
	try {
		// eslint-disable-next-line @typescript-eslint/no-var-requires
		const readline = require('readline');
		const createDiagnostics = (line: string) => {
			const parsers = [
				/*{
					regex: /(.*) return unit known from CMASI definition (.*)/,
					extra: ([_, functionName, unit]: string[]) => ({
						functionName: functionName,
						unit: unit,
					}),
				},*/
				{
					regex: /Incorrect store to variable (.*) in (.*) line ([0-9]+)\. (.*)/,
					parse: ([_, varName, filename, line]: string[]) => ({
						severity: DiagnosticSeverity.Error,
						range: {
							start: {line: parseInt(line)-1, character: 0},
							end: {line: parseInt(line), character: 0},
						},
						message: `Incorrect store to ${varName}.`,
						source: `${filename}`,
						data: {title: `Multiply ${varName} by 100.`, change: ' * 100'}
					}),
				},
				{
					regex: /Assignment to (.*) in (.*) on line ([0-9]+)/,
					parse: ([_, varName, filename, line]: string[]) => ({
							severity: DiagnosticSeverity.Error,
							range: {
								start: {line: parseInt(line)-1, character: 0},
								end: {line: parseInt(line), character: 0},
							},
							message: `Stores to ${varName}.`,
							source: `${filename}`,
							data: {title: `Multiply ${varName} by 100.`, change: ' * 100'},
					}),
				},
				{
					regex: /Call to (.*) in (.*) on line ([0-9]+)/,
					parse: ([_, functionName, filename, line]: string[]) => ({
							severity: DiagnosticSeverity.Error,
							range: {
								start: {line: parseInt(line)-1, character: 0},
								end: {line: parseInt(line), character: 0},
							},
							message: `Calls to ${functionName}.`,
							source: `${filename}`,
					}),
				},
			];

			for (const parser of parsers) {
				const maybeMatch = line.match(parser.regex);
				if (maybeMatch) {
					const encoded = encodeURI(filePath+parser.parse(maybeMatch).source.replace(/\/src/, ''));
					if (!diagnosticsMap.has(encoded))
						diagnosticsMap.set(encoded, []);
					diagnosticsMap.get(encoded)?.push(parser.parse(maybeMatch));
				}
			}
		};
		const sa4uConfig = await getSA4UConfig(path);
		console.log('using compile dir: ' + sa4uConfig.compilationDir);

		const child = exec(`docker container run --rm --mount type=bind,source="${path}",target="/src/" --name sa4u_z3_server_${path.replace(/([^A-Za-z0-9]+)/g, '')} sa4u-z3 -d True -c "${sa4uConfig.compilationDir}" -p /src/ex_prior.json -m /src/${messDef}`);
		const rl = readline.createInterface({input: child.stdout});
		rl.on('line', (line: any)=>{
			console.log(line);
			if (line.match(/---END RUN---/)) {
				diagnosticsMap.forEach((value, key) => {
					connection.sendDiagnostics({ uri: key, diagnostics: value});
					diagnosticsMap.set(key, []);
				});
			} else {
				createDiagnostics(line);
			}
		});
		startedSA4U_Z3 = true;
	} catch (e) {
		console.log(e);
	}
}

connection.onShutdown(() => {
	connection.workspace.getWorkspaceFolders().then((folders:null|WorkspaceFolder[]|void) => {
		if (folders) {
			folders.forEach((folder) => {
				let path = decodeURIComponent(folder.uri);
				path = path.replace(/(^\w+:|^)\/\//, '');
				path = path.replace(/:/, '');
				execSync(`docker container kill sa4u_z3_server_${path.replace(/([^A-Za-z0-9]+)/g, '')}`);
			});
		}
	});
});


// Make docker container and run it

// The example settings
interface ExampleSettings {
	maxNumberOfProblems: number;
}

// Cache the settings of all open documents
const documentSettings: Map<string, Thenable<ExampleSettings>> = new Map();

connection.onDidChangeConfiguration(change => {
	if (hasConfigurationCapability) {
		// Reset all cached document settings
		documentSettings.clear();
		if (change.settings.SA4U) {
			messDef = change.settings.SA4U.messageDefinition;
			if (startedSA4U_Z3) {
				startedSA4U_Z3 = false;
				connection.workspace.getWorkspaceFolders().then((folders:null|WorkspaceFolder[]|void) => {
					if (folders) {
						folders.forEach((folder) => {
							let path = decodeURIComponent(folder.uri);
							path = path.replace(/(^\w+:|^)\/\//, '');
							path = path.replace(/:/, '');
							execSync(`docker container kill sa4u_z3_server_${path.replace(/([^A-Za-z0-9]+)/g, '')}`);
							dockerContainer(folder);
						});
					}
				});
			}
		}
	}
});

// Only keep settings for open documents
documents.onDidClose(e => {
	documentSettings.delete(e.document.uri);
});

// The content of a text document has changed. This event is emitted
// when the text document first opened or when its content has changed.
documents.onDidSave(change => {
	validateTextDocument(change.document);
});

// Validate documents whenever they're opened.
documents.onDidOpen(e => {
	if (startedSA4U_Z3) 
		validateTextDocument(e.document);
});

async function validateTextDocument(textDocument: TextDocument): Promise<void> {
	connection.workspace.getWorkspaceFolders().then((folders:null|WorkspaceFolder[]|void) => {
		if (folders) {
			folders.forEach((folder) => {
				let path = decodeURIComponent(folder.uri);
				path = path.replace(/(^\w+:|^)\/\//, '');
				path = path.replace(/:/, '');
				execSync(`docker container kill -s=HUP sa4u_z3_server_${path.replace(/([^A-Za-z0-9]+)/g, '')}`);
			});
		}
	});
}

connection.onCodeAction((params) => {
	const textDocument = documents.get(params.textDocument.uri);
	if (textDocument === undefined) {
		return undefined;
	}
	if (typeof params.context.diagnostics[0] !== 'undefined' && typeof params.context.diagnostics[0].data !== 'undefined') {
		const data = (Object)(params.context.diagnostics[0].data);
		const title = data.title;
		return [CodeAction.create(title, Command.create(title, 'sa4u.fix', textDocument.uri, data.change, params.context.diagnostics[0].range), CodeActionKind.QuickFix)];
	}
});

connection.onExecuteCommand(async (params) => {
	if (params.arguments ===  undefined) {
		return;
	}
	if (params.command === 'sa4u.fix') {
		const textDocument = documents.get(params.arguments[0]);
		if (textDocument === undefined) {
			return;
		}
		const newText = params.arguments[1];
		if (typeof newText === 'string') {
			connection.workspace.applyEdit({
				documentChanges: [
					TextDocumentEdit.create({ uri: textDocument.uri, version: textDocument.version }, [
						TextEdit.insert(textDocument.positionAt(textDocument.offsetAt({line: params.arguments[2].end.line, character: params.arguments[2].end.character})-3), newText)
					])
				]
			});
		}
		//save the file
	}
});

connection.onDidChangeWatchedFiles(_change => {
	// Monitored files have change in VSCode
	connection.console.log('We received an file change event');
});

// Make the text document manager listen on the connection
// for open, change and close text document events
documents.listen(connection);

// Listen on the connection
connection.listen();
