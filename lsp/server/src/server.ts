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
	TextEdit
} from 'vscode-languageserver/node';

import {
	TextDocument
} from 'vscode-languageserver-textdocument';

import { exec, ExecException } from 'child_process';
import { promisify } from 'util';
// Create a connection for the server, using Node's IPC as a transport.
// Also include all preview / proposed LSP features.
const connection = createConnection(ProposedFeatures.all);

// Create a simple text document manager.
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;
let messDef:string;
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
				documents.all().forEach(validateTextDocument);
			}
		});
	}
	if (hasWorkspaceFolderCapability) {
		connection.workspace.onDidChangeWorkspaceFolders(_event => {
			connection.console.log('Workspace folder change event received.');
		});
	}
});

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
		if (change.settings.SA4U){
			messDef = change.settings.SA4U.messageDefinition;
			documents.all().forEach(validateTextDocument);
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
	validateTextDocument(e.document);
});

async function validateTextDocument(textDocument: TextDocument): Promise<void> {
	let path = decodeURIComponent(textDocument.uri);
	path = path.substring(0,path.lastIndexOf("/")+1);
	path = path.replace(/(^\w+:|^)\/\//, '');
	path = path.replace(/:/, '');
	const diagnostics: Diagnostic[] = [];
	const execPromise = promisify(exec);
	//const sa4uPromise = execPromise(`docker container run --rm -v ${path}:/src/ sa4u -c /src/ -p /src/ex_prior.json -m /src/CMASI.xml`);
	try {
		const sa4uZ3Promise = execPromise(`docker container run --rm --mount type=bind,source="${path}",target="/src/" sa4u-z3 -c /src/ -p /src/ex_prior.json -m /src/${messDef}`);
		//const sa4uOutput = await sa4uPromise;
		const sa4uZ3Output = await sa4uZ3Promise;

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
					diagnostics.push(parser.parse(maybeMatch));
					break;
				}
			}
		};

		const outputs = [
			//sa4uOutput,
			sa4uZ3Output,
		];
		for (const tool of outputs) {
			tool.stdout.split('\n').forEach(line => createDiagnostics(line));
		}

		connection.sendDiagnostics({ uri: textDocument.uri, diagnostics });
	} catch (e: unknown) {
		const error = e as {stderr:string, stdout:string};
		if (error.stderr) {
			connection.sendNotification("ServerError", error.stderr);
		}
	}
}

connection.onCodeAction((params) => {
	const textDocument = documents.get(params.textDocument.uri);
	if (textDocument === undefined) {
		return undefined;
	}
	const data = (Object)(params.context.diagnostics[0].data);
	const title = data.title;
	if (params.context.diagnostics[0].data) {
		return [CodeAction.create(title, Command.create(title, 'sa4u.fix', textDocument.uri, data.change, params.context.diagnostics[0].range), CodeActionKind.QuickFix)];
	}
});

connection.onExecuteCommand(async (params) => {
	if (params.arguments ===  undefined) {
		return;
	}
	if(params.command === 'sa4u.fix') {
		const textDocument = documents.get(params.arguments[0]);
		if (textDocument === undefined) {
			return;
		}
		const newText = params.arguments[1];
		if(typeof newText === 'string'){
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
