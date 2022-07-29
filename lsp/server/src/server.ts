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
import { constants, promises } from 'fs';
// Create a connection for the server, using Node's IPC as a transport.
// Also include all preview / proposed LSP features.
const connection = createConnection(ProposedFeatures.all);

// Create a simple text document manager.
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

// Docker image to use for analysis.
const dockerImageName = 'sa4u/sa4u:0.7.0';

// Location of config file
const CONFIG_FILE = '.sa4u/config.json';

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;
let allowConfigurationChanges = false;
let startedSA4U_Z3 = false;

const execAsync = promisify(exec);

class SA4UDebug {
	Image: string;
	MaintainContainerOnExit: boolean;
	constructor(image:string, maintainOnExit:boolean) {
		this.Image = image;
		this.MaintainContainerOnExit = maintainOnExit;
	}
}

class SA4UConfig {
	ProtocolDefinitionFile: string;
	CompilationDir: string;
	PriorTypes: string;
	IgnoreFiles: string[];
	Debug?:SA4UDebug;
	constructor(config: any) {
		if (config !== '') {
			this.ProtocolDefinitionFile = config.MessageDefinition || config.ProtocolDefinitionFile;
			this.CompilationDir = config.CompilationDir;
			this.PriorTypes = config.PriorTypes;
			if (typeof config.IgnoreFiles === 'string') {
				if (config.IgnoreFiles.replace(/\s/g, '') !== '') {
					this.IgnoreFiles = config.IgnoreFiles.split('\n');
				} else {
					this.IgnoreFiles = [];
				}
			} else {
				this.IgnoreFiles = config.IgnoreFiles || [];
			}
			if (config.DebugMode === true || config.Debug) {
				this.Debug = new SA4UDebug(config.DebugModeDockerImage || config.Debug?.Image, config.DebugModeMaintainContainerOnExit || config.Debug?.MaintainContainerOnExit);
			}
		} else {
			this.ProtocolDefinitionFile = '';
			this.CompilationDir = '';
			this.PriorTypes = '';
			this.IgnoreFiles = [];
		}
	}
	updateVSCodeConfig(): void {
		connection.sendNotification('UpdateConfig', { param: 'SA4U.MessageDefinition', value: this.ProtocolDefinitionFile });
		connection.sendNotification('UpdateConfig', { param: 'SA4U.CompilationDir', value: this.CompilationDir });
		connection.sendNotification('UpdateConfig', { param: 'SA4U.PriorTypes', value: this.PriorTypes });
		connection.sendNotification('UpdateConfig', { param: 'SA4U.IgnoreFiles', value: this.IgnoreFiles?.join('\n') || '' });
		if (this.Debug) {
			connection.sendNotification('UpdateConfig', { param: 'SA4U.DebugMode', value: true });
			connection.sendNotification('UpdateConfig', { param: 'SA4U.DebugModeDockerImage', value: this.Debug.Image });
			connection.sendNotification('UpdateConfig', { param: 'SA4U.DebugModeMaintainContainerOnExit', value: this.Debug.MaintainContainerOnExit });
		}
	}
	async writeToJSON(): Promise<void> {
		try {
			await promises.writeFile(CONFIG_FILE, JSON.stringify(this, null, '    '));
		} catch (err) {
			console.warn(`Failed to write to ${CONFIG_FILE}: ${err}`);
		}
	}
	returnParameters(folder: WorkspaceFolder): string {
		const path = getPath(folder, true);
		return `${(this.Debug?.MaintainContainerOnExit) ? '' : '--rm '}--mount type=bind,source="${path}",target="/src/" --name sa4u_z3_server_${path.replace(/([^A-Za-z0-9]+)/g, '')} ${this.Debug?.Image ?? dockerImageName} -d True -c "/src/${this.CompilationDir}" ${(this.IgnoreFiles.length === 0) ? '' : '-i '.concat(this.IgnoreFiles.join(' -i '))} -p /src/${this.PriorTypes} -m /src/${this.ProtocolDefinitionFile} --serialize-analysis /src/.sa4u/cache`;
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
				try {
					await execAsync(`docker container rm sa4u_z3_server_${path}`);
				} catch (err) {
					console.log(`Failed to remove a stopped docker container: ${err}`);
				}
				dockerContainer(folder);
			});
		}
	}
}

async function stopAndRemoveDockerContainer(): Promise<void> {
	let container_ids;
	try {
		container_ids = (await execAsync(`docker container ls -a -q -f name=sa4u_z3_server`)).stdout.split('\n').join(' ');
		await execAsync(`docker container stop ${container_ids}`);
	} catch (err) {
		console.log(err);
	}
	try {
		await execAsync(`docker container rm ${container_ids}`);
	} catch (err) {
		console.log(`Failed to remove a stopped docker container: ${err}`);
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

async function getConfig() {
	let readConfig: string;
	try {
		readConfig = (await promises.readFile(CONFIG_FILE)).toString();
	} catch (err) {
		throw `Unable to read ${CONFIG_FILE}: ${err}`;
	}
	let configJSON;
	try {
		configJSON = JSON.parse(readConfig);
	} catch (err) {
		throw `Failed to parse ${CONFIG_FILE} as a JSON: ${err}`;
	}
	return configJSON;
}

async function getSA4UConfig(): Promise<SA4UConfig> {
	let configJSON;
	try {
		configJSON = await getConfig();
	} catch (err) {
		console.warn(`${err}`);
		return new SA4UConfig('');
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
		configJSON['CompilationDir'] = '.sa4u/';
		return new SA4UConfig(configJSON);
	} catch (err) {
		console.warn(`Failed to write compile_commands.json into .sa4u directory: ${err}`);
		return new SA4UConfig(configJSON);
	}
}

async function updateConfigurations(): Promise<void> {
	if (!(await promises.access('.sa4u/', constants.F_OK).then(() => true, () => false))) {
		try {
			await promises.mkdir('.sa4u');
		} catch (err) {
			console.warn(`Failed to create .sa4u directory: ${err}`);
		}
	}
	if (!(await promises.access(CONFIG_FILE, constants.F_OK).then(() => true, () => false))) {
		const sa4uJSON = new SA4UConfig(await connection.workspace.getConfiguration('SA4U'));
		await sa4uJSON.writeToJSON();
	}
	try {
		const sa4uJSON = new SA4UConfig(await getConfig());
		sa4uJSON.updateVSCodeConfig();
	} catch (err) {
		console.warn(`${err}`);
	}
}

async function changedConfiguration(change: DidChangeConfigurationParams): Promise<void> {
	await (new SA4UConfig(change.settings.SA4U)).writeToJSON();
	restartDockerContainer();
}

async function dockerContainer(folder: WorkspaceFolder): Promise<void> {
	const filePath = decodeURIComponent(folder.uri);
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
		const sa4uConfig = await getSA4UConfig();
		const child = exec(`docker container run ${sa4uConfig.returnParameters(folder)}`);
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
	if (hasWorkspaceFolderCapability) {
		connection.workspace.onDidChangeWorkspaceFolders(_event => {
			connection.console.log('Workspace folder change event received.');
		});
	}
	Promise.all([checkForDockerRunningAndInstalled(), updateConfigurations(), stopAndRemoveDockerContainer()]).then(
		() => {startDockerContainer();}, 
		(err) => {connection.sendNotification('ServerError', `Server Failed to start Docker container: ${err}`);}
	);
	if (hasConfigurationCapability) {
		// Register for all configuration changes.
		connection.client.register(DidChangeConfigurationNotification.type, undefined);
	}
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
