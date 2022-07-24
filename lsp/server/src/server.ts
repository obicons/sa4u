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
	WorkspaceFolder,
	DidChangeConfigurationParams
} from 'vscode-languageserver/node';

import { resolve } from 'path';

import { TextDocument } from 'vscode-languageserver-textdocument';

import { exec, execSync } from 'child_process';
import { promisify } from 'util';
import { existsSync, promises } from 'fs';
// Create a connection for the server, using Node's IPC as a transport.
// Also include all preview / proposed LSP features.
const connection = createConnection(ProposedFeatures.all);

// Create a simple text document manager.
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

// Docker image to use for analysis.
const dockerImageName = "sa4u/sa4u:0.7.0";

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;
let messDef: string;
let allowConfigurationChanges = false;
let startedSA4U_Z3 = false;

const execAsync = promisify(exec);

class SA4UConfig {
	compilationDir: string
	constructor(compilationDir: string) {
		this.compilationDir = '/src/' + compilationDir;
	}
}

class SA4UIgnoredFiles {
	ignoreFilesCommand: string
	constructor(ignoreFilesArray: string[]) {
		if (ignoreFilesArray.length !== 0) {
			this.ignoreFilesCommand = '-i '.concat(ignoreFilesArray.join(' -i '));
		} else {
			this.ignoreFilesCommand = '';
		}
	} 
}

async function checkForDockerRunningAndInstalled(): Promise<void> {
	return await execAsync('docker info')
		.then(() => {return;}, () => {throw 'Please verify Docker is installed and Docker daemon is running.';});
}

async function startDockerContainer() {
	const folders = await connection.workspace.getWorkspaceFolders();
	if (folders) {
		folders.forEach((folder) => {
			dockerContainer(folder);
		});
	}
}

async function stopDockerContainer(): Promise<void> {
	let container_ids;
	try {
		container_ids = (await execAsync(`docker container ls -q -f name=sa4u_z3_server`)).stdout.split('\n').join(' ');
		await execAsync(`docker container stop ${container_ids}`);
	} catch (err) {
		console.log(err);
	}
}

async function getSA4UConfig(): Promise<SA4UConfig> {
	let readConfig: string;
	try {
		readConfig = (await promises.readFile('.sa4u.json')).toString();
	} catch (err) {
		console.warn('unable to read SA4U config: ' + err);
		return new SA4UConfig('');
	}
	let configJSON;
	try {
		configJSON = JSON.parse(readConfig);
	} catch (err) {
		console.warn(`Failed to parse .sa4u.json as a JSON: ${err}`);
		return new SA4UConfig('');
	}
	if (configJSON['CompilationDir'] === undefined) {
		return new SA4UConfig('');
	}
	if (!existsSync('.sa4u')){
		try {
			await promises.mkdir('.sa4u');
		} catch (err) {
			console.warn(`Failed to create .sa4u directory: ${err}`);
			return new SA4UConfig(configJSON['CompilationDir']);
		}
	}
	const directory = resolve('./');
	let readCompileCommands;
	try {
		readCompileCommands = (await promises.readFile(configJSON['CompilationDir']+'/compile_commands.json')).toString().replace(directory, '/src/');
	} catch (err) {
		console.warn(`Failed to read ${configJSON['CompilationDir']}/compile_commands.json: ${err}`);
		return new SA4UConfig('');
	}
	try {
		await promises.writeFile('.sa4u/compile_commands.json', readCompileCommands);
		return new SA4UConfig('.sa4u/');
	} catch (err) {
		console.warn(`Failed to write compile_commands.json into .sa4u directory: ${err}`);
		return new SA4UConfig(configJSON['CompilationDir']);
	}
}

async function getIgnoredFiles(): Promise<SA4UIgnoredFiles> {
	let readConfig: string;
	try {
		readConfig = (await promises.readFile('.sa4u.json')).toString();
	} catch (err) {
		console.warn('unable to read SA4U config: ' + err);
		return new SA4UIgnoredFiles([]);
	}
	let configJSON;
	try {
		configJSON = JSON.parse(readConfig);
		if (configJSON['IgnoreFiles'] === undefined) {
			return new SA4UIgnoredFiles([]);
		}
		return new SA4UIgnoredFiles(configJSON['IgnoreFiles']);
	} catch (err) {
		console.warn(`Failed to parse .sa4u.json as a JSON: ${err}`);
		return new SA4UIgnoredFiles([]);
	}
}

async function updateMessageDefinitionFromFile(): Promise<void> {
	try {
		const readFile = (await promises.readFile('.sa4u.json')).toString();
		const configJSON = JSON.parse(readFile);
		messDef = (configJSON['ProtocolDefinitionFile']?configJSON['ProtocolDefinitionFile']:'');
	} catch (err) {
		console.warn(`Failed to read .sa4u.json: ${err}`);
	}
	if (messDef == '') {
		const config = await connection.workspace.getConfiguration('SA4U');
		if (config.messageDefinition)
			messDef = config.messageDefinition;
	}
}

async function dockerContainer(folder: WorkspaceFolder): Promise<void> {
	const filePath = decodeURIComponent(folder.uri);
	const path = getPath(folder, true);
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
		const sa4uConfig = await Promise.all([getSA4UConfig(), getIgnoredFiles()]).then((value) => {return {compilationDir: value[0].compilationDir, ignore: value[1].ignoreFilesCommand};});
		console.log(`using compile dir: ${sa4uConfig.compilationDir}`);
		const child = exec(`docker container run --rm --mount type=bind,source="${path}",target="/src/" --name sa4u_z3_server_${path.replace(/([^A-Za-z0-9]+)/g, '')} ${dockerImageName} -d True -c "${sa4uConfig.compilationDir}" ${sa4uConfig.ignore} -p /src/ex_prior.json -m /src/${messDef}`);
		const rl = readline.createInterface({input: child.stdout});
		rl.on('line', (line: any) => {
			console.log(line);
			if (line.match(/---END RUN---/)) {
				diagnosticsMap.forEach((value, key) => {
					connection.sendDiagnostics({uri: key, diagnostics: value});
					diagnosticsMap.set(key, []);
				});
			} else {
				createDiagnostics(line);
			}
		});
		startedSA4U_Z3 = true;
	} catch (e) {
		connection.sendNotification('ServerError', `Failed to start the docker container: ${e}`);
	}
}

async function changedConfiguration(change: DidChangeConfigurationParams): Promise<void> {
	messDef = change.settings.SA4U.messageDefinition;
	try {
		const readSA4U = (await promises.readFile('.sa4u.json')).toString();
		const configJSON = JSON.parse(readSA4U);
		configJSON['ProtocolDefinitionFile'] = messDef;
		await promises.writeFile('.sa4u.json', JSON.stringify(configJSON, null, '    '));
	} catch (err) {
		console.warn(`Failed to update .sa4u file: ${err}`);
	}
	restartDockerContainer();
}

async function restartDockerContainer(): Promise<void> {
	if (startedSA4U_Z3) {
		startedSA4U_Z3 = false;
		const folders = await connection.workspace.getWorkspaceFolders();
		if (folders) {
			folders.forEach(async (folder) => {
				const path = getPath(folder);
				try {
					await execAsync(`docker container stop sa4u_z3_server_${path}`);
				} catch (err) {
					console.log(`Failed to stop the docker container: ${err}`);
				}
				dockerContainer(folder);
			});
		}
	}
}

async function validateTextDocument(textDocument: TextDocument): Promise<void> {
	const folders = await connection.workspace.getWorkspaceFolders();
	if (folders) {
		folders.forEach(async (folder) => {
			const path = getPath(folder);
			try {
				await execAsync(`docker container kill -s=HUP sa4u_z3_server_${path}`);
			} catch (err) {
				console.warn(`Failed to send HUP signal to the contianer: ${err}`);
			}
		});
	}
}

function getPath (folder: WorkspaceFolder, basic = false): string {
	let path = decodeURIComponent(folder.uri);
	path = path.replace(/(^\w+:|^)\/\//, '');
	path = path.replace(/:/, '');
	if (basic)
		return path;
	path = path.replace(/([^A-Za-z0-9]+)/g, '');
	return path;
}

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
	allowConfigurationChanges = false;
	if (hasConfigurationCapability) {
		// Register for all configuration changes.
		connection.client.register(DidChangeConfigurationNotification.type, undefined);
		
	}
	if (hasWorkspaceFolderCapability) {
		connection.workspace.onDidChangeWorkspaceFolders(_event => {
			connection.console.log('Workspace folder change event received.');
		});
	}
	Promise.all([checkForDockerRunningAndInstalled(), updateMessageDefinitionFromFile(), stopDockerContainer()])
		.then(() => {startDockerContainer();}, (err) => {connection.sendNotification('ServerError', `Server Failed to start Docker container: ${err}`);});
});

connection.onDidChangeConfiguration(change => {
	if (hasConfigurationCapability) {
		if (change.settings.SA4U) {
			if (allowConfigurationChanges) {
				changedConfiguration(change);
			} else {
				allowConfigurationChanges = true;
			}
		}
	}
});

connection.onCodeAction((params) => {
	const textDocument = documents.get(params.textDocument.uri);
	if (textDocument === undefined) {
		return undefined;
	}
	if (params.context.diagnostics[0] !== undefined && params.context.diagnostics[0].data !== undefined) {
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

connection.onShutdown(() => {
	connection.workspace.getWorkspaceFolders().then((folders: null|WorkspaceFolder[]|void) => {
		if (folders) {
			folders.forEach((folder) => {
				const path = getPath(folder);
				try {
					execSync(`docker container stop sa4u_z3_server_${path}`);
				} catch (err) {
					console.warn(`Failed to stop the docker container: ${err}`);
				}
			});
		}
	});
});

// The content of a text document has changed. This event is emitted
// when the text document first opened or when its content has changed.
documents.onDidSave(change => {
	validateTextDocument(change.document);
});

// Make the text document manager listen on the connection
// for open, change and close text document events
documents.listen(connection);

// Listen on the connection
connection.listen();
