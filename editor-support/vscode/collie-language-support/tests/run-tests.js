const fs = require('fs');
const path = require('path');
const vsctm = require('vscode-textmate');
const oniguruma = require('oniguruma');

async function main() {
    // 初始化语法注册表
    const registry = new vsctm.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: (sources) => new oniguruma.OnigScanner(sources),
            createOnigString: (str) => new oniguruma.OnigString(str)
        }),
        loadGrammar: async (scopeName) => {
            const grammarPath = path.join(__dirname, '..', 'syntaxes', 'collie.tmLanguage.json');
            const grammar = JSON.parse(fs.readFileSync(grammarPath, 'utf8'));
            return grammar;
        }
    });

    // 加载语法
    const grammar = await registry.loadGrammar('source.collie');
    if (!grammar) {
        console.error('Failed to load grammar');
        process.exit(1);
    }

    // 获取所有测试文件
    const testDir = path.join(__dirname, 'syntax');
    const testFiles = fs.readdirSync(testDir)
        .filter(file => file.endsWith('.test.collie'))
        .map(file => path.join(testDir, file));

    let failedTests = 0;
    let passedTests = 0;
    let currentFile = '';
    let hasFailuresInCurrentFile = false;

    // 运行每个测试文件
    for (const testFile of testFiles) {
        const content = fs.readFileSync(testFile, 'utf8');
        const lines = content.split('\n');
        let ruleStack = vsctm.INITIAL;
        let lastCodeLine = '';
        let lastTokens = [];

        currentFile = path.basename(testFile);
        hasFailuresInCurrentFile = false;

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i].trimRight();
            if (line.trim().startsWith('# SYNTAX TEST')) continue;

            // 如果是断言行，检查上一行的语法标记
            if (line.trim().startsWith('#')) {
                // 解析断言
                const assertionMatch = line.match(/^#(\s*\^+)\s*(.+)$/);
                if (assertionMatch) {
                    const [_, carets, expectedScope] = assertionMatch;
                    const startPos = line.indexOf('^');
                    const endPos = startPos + carets.trim().length;

                    // 找到对应位置的token
                    let found = false;
                    for (const token of lastTokens) {
                        if (startPos >= token.startIndex && startPos < token.endIndex) {
                            const scope = token.scopes[token.scopes.length - 1];
                            const text = lastCodeLine.substring(token.startIndex, token.endIndex);
                            if (scope === expectedScope.trim()) {
                                passedTests++;
                            } else {
                                if (!hasFailuresInCurrentFile) {
                                    console.log(`\nFailures in ${currentFile}:`);
                                    hasFailuresInCurrentFile = true;
                                }
                                console.log(`\nFailed assertion at line ${i + 1}:`);
                                console.log(`Code line: "${lastCodeLine}"`);
                                console.log(`Assertion: "${line}"`);
                                console.log(`Expected scope: ${expectedScope.trim()}`);
                                console.log(`Actual scope: ${scope}`);
                                console.log(`Text: "${text}"`);
                                console.log(`Position: ${startPos}-${endPos} (token: ${token.startIndex}-${token.endIndex})`);
                                failedTests++;
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        if (!hasFailuresInCurrentFile) {
                            console.log(`\nFailures in ${currentFile}:`);
                            hasFailuresInCurrentFile = true;
                        }
                        console.log(`\nFailed assertion at line ${i + 1}:`);
                        console.log(`No token found at position ${startPos}-${endPos}`);
                        console.log(`Code line: "${lastCodeLine}"`);
                        console.log(`Assertion: "${line}"`);
                        failedTests++;
                    }
                }
            } else {
                // 保存代码行和其tokens以供后续断言使用
                lastCodeLine = line;
                lastTokens = grammar.tokenizeLine(line, ruleStack).tokens;
                ruleStack = grammar.tokenizeLine(line, ruleStack).ruleStack;
            }
        }
    }

    // 打印总结
    console.log('\nTest Summary:');
    if (failedTests > 0) {
        console.log(`  ✗ ${failedTests} tests failed`);
        console.log(`  ✓ ${passedTests} tests passed`);
    } else {
        console.log(`  ✓ All ${passedTests} tests passed`);
    }
    console.log(`  Total: ${passedTests + failedTests} tests`);

    if (failedTests > 0) {
        process.exit(1);
    }
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});