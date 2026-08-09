// Lumi 를 VS Code 에 붙이는 아주 얇은 껍데기.
// 진짜 일은 전부 `lumi lsp` 가 합니다 — 여기서는 그 프로그램을 켜고
// 표준입출력을 이어 주기만 합니다.  그래서 언어에 낱말이 늘어도
// 이 파일은 손댈 것이 없습니다.
//
// npm 으로 받아 오는 것이 하나도 없도록 LSP 대화를 직접 적었습니다.
// (vscode-languageclient 를 쓰면 더 짧지만, 이 저장소는 '의존성 없음'을 지킵니다.)

const vscode = require("vscode");
const { spawn } = require("child_process");

let proc = null;
let diagnostics = null;
let nextId = 1;
const waiting = new Map();          // id -> resolve

function send(msg) {
  if (!proc) return;
  const body = Buffer.from(JSON.stringify(msg), "utf8");
  proc.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
  proc.stdin.write(body);
}

function ask(method, params) {
  const id = nextId++;
  return new Promise((resolve) => {
    waiting.set(id, resolve);
    send({ jsonrpc: "2.0", id, method, params });
    setTimeout(() => { if (waiting.delete(id)) resolve(null); }, 3000);
  });
}

function onMessage(msg) {
  if (msg.id !== undefined && waiting.has(msg.id)) {
    waiting.get(msg.id)(msg.result);
    waiting.delete(msg.id);
    return;
  }
  if (msg.method === "textDocument/publishDiagnostics") {
    const p = msg.params;
    const uri = vscode.Uri.parse(p.uri);
    diagnostics.set(uri, (p.diagnostics || []).map((d) => {
      const r = d.range;
      return new vscode.Diagnostic(
        new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character),
        d.message,
        vscode.DiagnosticSeverity.Error
      );
    }));
  }
}

function activate(context) {
  diagnostics = vscode.languages.createDiagnosticCollection("lumi");
  context.subscriptions.push(diagnostics);

  const exe = vscode.workspace.getConfiguration("lumi").get("path") || "lumi";

  // 디버거는 `lumi dap` 이 전부입니다 — VS Code 에 그 프로그램을 알려 주기만 합니다.
  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("lumi", {
    createDebugAdapterDescriptor() {
      return new vscode.DebugAdapterExecutable(exe, ["dap"]);
    },
  }));
  // launch.json 이 없어도 F5 가 되도록, 지금 열린 파일을 넣어 줍니다.
  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider("lumi", {
    resolveDebugConfiguration(folder, config) {
      if (!config.type) {
        const doc = vscode.window.activeTextEditor && vscode.window.activeTextEditor.document;
        if (!doc || doc.languageId !== "lumi") return undefined;
        config.type = "lumi";
        config.request = "launch";
        config.name = "Run this Lumi file";
        config.program = doc.fileName;
      }
      return config;
    },
  }));

  try {
    proc = spawn(exe, ["lsp"], { stdio: ["pipe", "pipe", "pipe"] });
  } catch (e) {
    vscode.window.showErrorMessage(`Lumi: cannot start "${exe} lsp" - set lumi.path in settings.`);
    return;
  }
  proc.on("error", () => {
    vscode.window.showErrorMessage(`Lumi: cannot start "${exe} lsp" - set lumi.path in settings.`);
  });

  // 들어오는 덩어리를 Content-Length 대로 잘라 읽습니다
  let buf = Buffer.alloc(0);
  proc.stdout.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      const head = buf.indexOf("\r\n\r\n");
      if (head < 0) return;
      const m = /Content-Length:\s*(\d+)/i.exec(buf.slice(0, head).toString("ascii"));
      if (!m) { buf = buf.slice(head + 4); continue; }
      const len = parseInt(m[1], 10);
      if (buf.length < head + 4 + len) return;
      const body = buf.slice(head + 4, head + 4 + len).toString("utf8");
      buf = buf.slice(head + 4 + len);
      try { onMessage(JSON.parse(body)); } catch (e) { /* 깨진 덩어리는 넘깁니다 */ }
    }
  });

  send({ jsonrpc: "2.0", id: 0, method: "initialize", params: { capabilities: {} } });
  send({ jsonrpc: "2.0", method: "initialized", params: {} });

  const open = (doc) => {
    if (doc.languageId !== "lumi") return;
    send({ jsonrpc: "2.0", method: "textDocument/didOpen", params: {
      textDocument: { uri: doc.uri.toString(), languageId: "lumi",
                      version: doc.version, text: doc.getText() } } });
  };
  vscode.workspace.textDocuments.forEach(open);
  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(open));

  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((e) => {
    if (e.document.languageId !== "lumi") return;
    send({ jsonrpc: "2.0", method: "textDocument/didChange", params: {
      textDocument: { uri: e.document.uri.toString(), version: e.document.version },
      contentChanges: [{ text: e.document.getText() }] } });
  }));

  context.subscriptions.push(vscode.workspace.onDidCloseTextDocument((doc) => {
    if (doc.languageId !== "lumi") return;
    send({ jsonrpc: "2.0", method: "textDocument/didClose", params: {
      textDocument: { uri: doc.uri.toString() } } });
  }));

  context.subscriptions.push(vscode.languages.registerCompletionItemProvider("lumi", {
    async provideCompletionItems(doc, pos) {
      const items = await ask("textDocument/completion", {
        textDocument: { uri: doc.uri.toString() },
        position: { line: pos.line, character: pos.character },
      });
      if (!items) return [];
      return items.map((it) => {
        const c = new vscode.CompletionItem(it.label);
        if (it.kind) c.kind = it.kind - 1;      // LSP 는 1부터, VS Code 는 0부터
        c.detail = it.detail;
        c.documentation = it.documentation;
        return c;
      });
    },
  }, "."));

  context.subscriptions.push(vscode.languages.registerHoverProvider("lumi", {
    async provideHover(doc, pos) {
      const h = await ask("textDocument/hover", {
        textDocument: { uri: doc.uri.toString() },
        position: { line: pos.line, character: pos.character },
      });
      if (!h || !h.contents) return null;
      return new vscode.Hover(new vscode.MarkdownString(h.contents.value));
    },
  }));

  context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider("lumi", {
    async provideDocumentSymbols(doc) {
      const syms = await ask("textDocument/documentSymbol", {
        textDocument: { uri: doc.uri.toString() },
      });
      if (!syms) return [];
      return syms.map((s) => {
        const r = s.range;
        return new vscode.DocumentSymbol(
          s.name, "", s.kind - 1,
          new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character),
          new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character)
        );
      });
    },
  }));
}

function deactivate() {
  if (proc) {
    send({ jsonrpc: "2.0", id: 9999, method: "shutdown", params: {} });
    send({ jsonrpc: "2.0", method: "exit", params: {} });
    proc.kill();
    proc = null;
  }
}

module.exports = { activate, deactivate };
