# SA4U
This is the SA4U editor plugin. It helps developers find unit type errors (UTEs) before they are triggered in production.

## Structure
```
.
├── client // Language Client
│   ├── src
│   │   ├── test // End to End tests for Language Client / Server
│   │   └── extension.ts // Language Client entry point
├── package.json // The extension manifest.
└── server // Language Server
    └── src
        └── server.ts // Language Server entry point
```

## Running the extension
- Run `npm install` in this folder. This installs all necessary npm modules in both the client and server folder
- Open VS Code on this folder.
- Press Ctrl+Shift+B (Cmd+Shift+B on Mac) to compile the client and server.
- Switch to the Debug viewlet.
- Select `Client + Server` from the drop down.
- Run the launch config.
- In the [Extension Development Host] instance of VSCode, open a folder containing a C++ project.
