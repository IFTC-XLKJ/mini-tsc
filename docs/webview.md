# webview 模块 - 原生 Web 视图

## 概述

`webview` 模块提供了原生 Web 视图功能，允许创建桌面应用程序，使用 HTML/CSS/JavaScript 构建用户界面。

## 类型定义

```typescript
interface WebViewOptions {
  title?: string;
  width?: number;
  height?: number;
  resizable?: boolean;
  debug?: boolean;
  url?: string;
}

interface WebView {
  loadURL(url: string): void;
  navigate(url: string): void;
  loadHTML(html: string): void;
  evaluate(script: string): void;
  executeJavaScript(script: string): void;
  setTitle(title: string): void;
  setSize(width: number, height: number): void;
  setIcon(iconPath: string): void;
  setPosition(x: number, y: number): void;
  center(): void;
  show(): void;
  hide(): void;
  focus(): void;
  minimize(): void;
  maximize(): void;
  unmaximize(): void;
  close(): void;
  run(): void;
  
  on(event: string, callback: Function): this;
  once(event: string, callback: Function): this;
  off(event: string, callback: Function): this;
  
  readonly ready: boolean;
  readonly url: string;
  
  addJavaScriptInterface(name: string, code: string): void;
  removeJavaScriptInterface(name: string): void;
}
```

## API 列表

### 模块级方法

#### `webview.isAvailable()`

检查 WebView 是否可用。

**返回值**: `boolean`

**示例**:
```typescript
import { webview } from 'webview';

if (webview.isAvailable()) {
  console.log('WebView is available');
} else {
  console.log('WebView is not available on this system');
}
```

---

#### `new webview.WebView(options?)`

创建 WebView 实例。

**参数**:
- `options`: 配置选项

**返回值**: `WebView` 对象

**示例**:
```typescript
import { webview } from 'webview';

const w = new webview.WebView({
  title: 'My App',
  width: 800,
  height: 600,
  resizable: true
});
```

---

### 实例方法

#### `w.loadURL(url)`

加载 URL。

**参数**:
- `url`: 要加载的 URL

**返回值**: `void`

**示例**:
```typescript
w.loadURL('https://example.com');
```

---

#### `w.navigate(url)`

导航到 URL（别名）。

**参数**:
- `url`: 要导航的 URL

**返回值**: `void`

---

#### `w.loadHTML(html)`

加载 HTML 内容。

**参数**:
- `html`: HTML 字符串

**返回值**: `void`

**示例**:
```typescript
w.loadHTML(`
  <!DOCTYPE html>
  <html>
  <head>
    <title>Hello</title>
    <style>
      body { font-family: Arial, sans-serif; padding: 20px; }
      h1 { color: #333; }
    </style>
  </head>
  <body>
    <h1>Hello from WebView!</h1>
    <p>This is a native desktop application.</p>
  </body>
  </html>
`);
```

---

#### `w.evaluate(script)`

执行 JavaScript 代码。

**参数**:
- `script`: JavaScript 代码

**返回值**: `void`

**示例**:
```typescript
w.evaluate(`
  document.body.style.backgroundColor = '#f0f0f0';
  console.log('Hello from TypeScript!');
`);
```

---

#### `w.executeJavaScript(script)`

执行 JavaScript 代码（别名）。

---

#### `w.setTitle(title)`

设置窗口标题。

**参数**:
- `title`: 标题文本

**返回值**: `void`

**示例**:
```typescript
w.setTitle('My Application - Version 1.0');
```

---

#### `w.setSize(width, height)`

设置窗口大小。

**参数**:
- `width`: 宽度（像素）
- `height`: 高度（像素）

**返回值**: `void`

**示例**:
```typescript
w.setSize(1024, 768);
```

---

#### `w.setIcon(iconPath)`

设置窗口图标。

**参数**:
- `iconPath`: 图标文件路径

**返回值**: `void`

---

#### `w.setPosition(x, y)`

设置窗口位置。

**参数**:
- `x`: X 坐标
- `y`: Y 坐标

**返回值**: `void`

---

#### `w.center()`

将窗口居中。

**返回值**: `void`

---

#### `w.show()`

显示窗口。

**返回值**: `void`

---

#### `w.hide()`

隐藏窗口。

**返回值**: `void`

---

#### `w.focus()`

聚焦窗口。

**返回值**: `void`

---

#### `w.minimize()`

最小化窗口。

**返回值**: `void`

---

#### `w.maximize()`

最大化窗口。

**返回值**: `void`

---

#### `w.unmaximize()`

取消最大化窗口。

**返回值**: `void`

---

#### `w.close()`

关闭窗口。

**返回值**: `void`

---

#### `w.run()`

运行 WebView 事件循环。

**返回值**: `void`

**示例**:
```typescript
w.loadHTML('<h1>Hello!</h1>');
w.run();
```

---

#### `w.on(event, callback)`

监听事件。

**参数**:
- `event`: 事件名称
- `callback`: 回调函数

**返回值**: `this`

---

#### `w.addJavaScriptInterface(name, code)`

添加 JavaScript 接口。

**参数**:
- `name`: 接口名称
- `code`: JavaScript 代码

**返回值**: `void`

**示例**:
```typescript
w.addJavaScriptInterface('myApi', `
  function greet(name) {
    return 'Hello, ' + name + '!';
  }
`);
```

---

#### `w.removeJavaScriptInterface(name)`

移除 JavaScript 接口。

**参数**:
- `name`: 接口名称

**返回值**: `void`

---

## 使用示例

### 基本窗口

```typescript
import { webview } from 'webview';

const w = new webview.WebView({
  title: 'My App',
  width: 800,
  height: 600
});

w.loadHTML(`
  <!DOCTYPE html>
  <html>
  <head>
    <title>My App</title>
    <style>
      body {
        font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
        margin: 0;
        padding: 20px;
        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
        color: white;
        min-height: 100vh;
        box-sizing: border-box;
      }
      h1 { margin-top: 0; }
      .card {
        background: rgba(255,255,255,0.2);
        border-radius: 10px;
        padding: 20px;
        margin: 10px 0;
      }
    </style>
  </head>
  <body>
    <h1>Welcome to My App</h1>
    <div class="card">
      <h2>Getting Started</h2>
      <p>This is a native desktop application built with WebView.</p>
    </div>
    <div class="card">
      <h2>Features</h2>
      <ul>
        <li>Native performance</li>
        <li>Web technologies</li>
        <li>Cross-platform</li>
      </ul>
    </div>
  </body>
  </html>
`);

w.run();
```

### 与 TypeScript 交互

**main.ts**:
```typescript
import { webview } from 'webview';

const w = new webview.WebView({
  title: 'Interactive App',
  width: 600,
  height: 400
});

// 添加 JavaScript 接口
w.addJavaScriptInterface('app', `
  function calculate(a, b, operation) {
    switch(operation) {
      case 'add': return a + b;
      case 'subtract': return a - b;
      case 'multiply': return a * b;
      case 'divide': return a / b;
      default: return 0;
    }
  }
  
  function greet(name) {
    return 'Hello, ' + name + '! Welcome to the app.';
  }
`);

w.loadHTML(`
  <!DOCTYPE html>
  <html>
  <head>
    <title>Calculator</title>
    <style>
      body { font-family: sans-serif; padding: 20px; }
      input { margin: 5px; padding: 8px; }
      button { margin: 5px; padding: 8px 16px; cursor: pointer; }
      #result { margin-top: 20px; padding: 10px; background: #f0f0f0; }
    </style>
  </head>
  <body>
    <h1>Calculator</h1>
    <input type="number" id="num1" placeholder="Number 1">
    <input type="number" id="num2" placeholder="Number 2">
    <div>
      <button onclick="doCalc('add')">+</button>
      <button onclick="doCalc('subtract')">-</button>
      <button onclick="doCalc('multiply')">*</button>
      <button onclick="doCalc('divide')">/</button>
    </div>
    <div id="result">Result: </div>
    
    <script>
      function doCalc(op) {
        const a = parseFloat(document.getElementById('num1').value) || 0;
        const b = parseFloat(document.getElementById('num2').value) || 0;
        const result = app.calculate(a, b, op);
        document.getElementById('result').textContent = 'Result: ' + result;
      }
    </script>
  </body>
  </html>
`);

w.run();
```

### 多窗口应用

```typescript
import { webview } from 'webview';

class MultiWindowApp {
  private windows: Map<string, webview.WebView> = new Map();
  
  createWindow(id: string, options: any): webview.WebView {
    const w = new webview.WebView(options);
    this.windows.set(id, w);
    
    w.on('close', () => {
      this.windows.delete(id);
      if (this.windows.size === 0) {
        process.exit(0);
      }
    });
    
    return w;
  }
  
  getWindow(id: string): webview.WebView | undefined {
    return this.windows.get(id);
  }
  
  closeAll(): void {
    for (const w of this.windows.values()) {
      w.close();
    }
    this.windows.clear();
  }
}

// 使用示例
const app = new MultiWindowApp();

// 主窗口
const main = app.createWindow('main', {
  title: 'Main Window',
  width: 800,
  height: 600
});

main.loadHTML('<h1>Main Window</h1><button onclick="window.open(\'child\')">Open Child</button>');
main.run();

// 子窗口
const child = app.createWindow('child', {
  title: 'Child Window',
  width: 400,
  height: 300
});

child.loadHTML('<h1>Child Window</h1>');
```

### 文件查看器

```typescript
import { webview } from 'webview';
import * as fs from 'fs';
import * as path from 'path';

function createFileViewer(filePath: string): void {
  const content = fs.readFileSync(filePath, 'utf-8');
  const ext = path.extname(filePath).toLowerCase();
  
  let html = '';
  
  if (['.html', '.htm'].includes(ext)) {
    html = content;
  } else if (['.js', '.ts', '.py', '.java', '.c', '.cpp'].includes(ext)) {
    html = `
      <!DOCTYPE html>
      <html>
      <head>
        <title>${path.basename(filePath)}</title>
        <style>
          body { margin: 0; padding: 0; }
          pre {
            margin: 0;
            padding: 20px;
            background: #1e1e1e;
            color: #d4d4d4;
            font-family: 'Consolas', monospace;
            overflow: auto;
            min-height: 100vh;
            box-sizing: border-box;
          }
        </style>
      </head>
      <body>
        <pre>${escapeHtml(content)}</pre>
      </body>
      </html>
    `;
  } else {
    html = `
      <!DOCTYPE html>
      <html>
      <head>
        <title>${path.basename(filePath)}</title>
        <style>
          body { font-family: sans-serif; padding: 20px; }
        </style>
      </head>
      <body>
        <h1>${path.basename(filePath)}</h1>
        <p>Preview not available for this file type.</p>
        <pre>${escapeHtml(content.slice(0, 1000))}</pre>
      </body>
      </html>
    `;
  }
  
  const w = new webview.WebView({
    title: path.basename(filePath),
    width: 900,
    height: 700
  });
  
  w.loadHTML(html);
  w.run();
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

// 使用示例
createFileViewer('example.ts');
```

## 事件

### WebView 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'ready'` | WebView 准备就绪 | 无 |
| `'close'` | 窗口关闭 | 无 |
| `'navigate'` | 页面导航 | `url: string` |

## 实现细节

### 平台支持

| 平台 | 后端 | 状态 |
|------|------|------|
| Windows | WebView2 (Edge) | 完整支持 |
| macOS | WebKit | 完整支持 |
| Linux | WebKitGTK | 完整支持 |

### 架构

```
┌─────────────────────────────────────┐
│           TypeScript/JS             │
├─────────────────────────────────────┤
│           WebView API               │
├─────────────────────────────────────┤
│     Native WebView Backend          │
├─────────────────────────────────────┤
│  WebView2 (Win) / WebKit (Mac/Linux)│
└─────────────────────────────────────┘
```

## 依赖项

### Windows
- WebView2 Runtime（通常预装在 Windows 10/11）

### macOS
- WebKit（系统自带）

### Linux
- WebKitGTK
- GTK3

安装依赖（Ubuntu/Debian）：
```bash
sudo apt-get install libwebkit2gtk-4.1-dev libgtk-3-dev
```

## 性能优化

1. **减少 DOM 操作**: 批量更新 DOM
2. **使用虚拟滚动**: 大列表使用虚拟滚动
3. **避免内存泄漏**: 及时清理事件监听器
4. **使用 Web Worker**: 密集计算使用 Web Worker

## 常见问题

### WebView 不可用

```
Error: WebView is not available
```

**解决方案**: 安装必要的系统依赖

### 内存占用高

**解决方案**:
- 减少 DOM 节点数量
- 使用 CSS 动画代替 JavaScript 动画
- 及时释放不需要的资源

### 跨域问题

**解决方案**:
- 使用代理服务器
- 配置 CORS 头
- 使用本地文件协议
