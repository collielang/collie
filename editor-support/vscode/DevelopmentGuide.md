# VSCode 编辑器支持

> Collie language support for Visual Studio Code.

扩展名：`Collie Language Support`

标识符：`collie-language-support` (<del>`collie.collie`</del>)


插件初始化：

```bash
npm install -g yo generator-code
yo code
```

```bash
#> yo code

     _-----_     ╭──────────────────────────╮
    |       |    │   Welcome to the Visual  │
    |--(o)--|    │   Studio Code Extension  │
   `---------´   │        generator!        │
    ( _´U`_ )    ╰──────────────────────────╯
    /___A___\   /
     |  ~  |
   __'.___.'__
 ´   `  |° ´ Y `

? What type of extension do you want to create? New Language Support
Enter the URL (http, https) or the file path of the tmLanguage grammar or press ENTER to start with a new grammar.
? URL or file to import, or none for new:
? What's the name of your extension? Collie Language Support
? What's the identifier of your extension? collie-language-support
? What's the description of your extension? Collie language support for Visual Studio Code.
Enter the id of the language. The id is an identifier and is single, lower-case name such as 'php', 'javascript'
? Language id: collie
Enter the name of the language. The name will be shown in the VS Code editor mode selector.
? Language name: Collie
Enter the file extensions of the language. Use commas to separate multiple entries (e.g. .ruby, .rb)
? File extensions: .collie, .co
Enter the root scope name of the grammar (e.g. source.ruby)
? Scope names: source.collie
? Initialize a git repository? No

```
