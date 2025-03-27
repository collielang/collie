import type * as PrismNamespace from 'prismjs';

/**
 * 语法高亮
 *
 * refer: https://github.com/PrismJS/prism/blob/master/components
 *
 * - Java: https://github.com/PrismJS/prism/blob/master/components/prism-java.js
 * - JavaScript: https://github.com/PrismJS/prism/blob/master/components/prism-javascript.js
 */
const addColliePrismLanguageSupport = function (PrismObject: typeof PrismNamespace): void {
  const PrismLanguagesCollie = PrismObject.languages.extend('clike', {
    'comment': [
      // 匹配以 /... 开头，.../ 结尾的块注释
      {
        pattern: /\/\.\.\.[\s\S]*?\.\.\.\//,
        greedy: true,
        inside: {
          'nested-comment': {
            pattern: /\/\/|\/\*|\*\//,
          }
        }
      },
      // 文档注释（从 /... 到 .../）
      {
        pattern: /\/\.\.\.[\s\S]*?\.\.\.\//,
        greedy: true,
      },
      // 匹配以 /... 开头直到文档末尾的块注释
      {
        pattern: /\/\.\.\.[\s\S]*/,
        greedy: true,
      },
      {
        pattern: /^(?![^\n]*\/\.\.\.)[\s\S]*?\.\.\.\//,
        greedy: true,
        alias: 'comment',
        lookbehind: false
      },
      // // 匹配以文档开头直到 .../ 结尾的块注释
      // {
      //   pattern: /[\s\S]*\.\.\.\//,
      //   greedy: true,
      // },
      // 单行注释
      {
        // 匹配: //xxx
        pattern: /\/\/.*/,
        greedy: true
      },
      // // 多行注释（支持嵌套1层）
      // {
      //   // 匹配: /*x/*xxx*/x*/
      //   pattern: /\/\*(?:[^*/]|\*(?!\/)|\/(?!\*)|\/\*[\s\S]*?\*\/)*\*\//,
      //   greedy: true,
      //   inside: {
      //     // 注释内部允许嵌套其他注释（如字符串或关键字）
      //     'nested-comment': {
      //       // 匹配: /*xxx*/
      //       pattern: /\/\*[\s\S]*?\*\//,
      //       greedy: true,
      //     }
      //   }
      // },
      // 多行注释
      {
        // 匹配: /*xxx*/
        pattern: /\/\*[\s\S]*?\*\//,
        greedy: true
      },
    ],

    // 关键字列表
    'keyword': /\b(?:if|else|while|for|function|return)\b/,

    // 其他规则
    // 'string': {
    //   pattern: /(["'])(?:\\(?:\r\n|[\s\S])|(?!\1)[^\\\r\n])*\1/,
    //   greedy: true
    // },
    // 'keyword': /\b(?:if|else|while|for|function|return|class|let|const|var|new|this)\b/,
    // 'boolean': /\b(?:true|false)\b/,
    // 'number': /\b0x[\da-f]+\b|(?:\b\d+\.?\d*|\B\.\d+)(?:e[+-]?\d+)?/i,
    // 'operator': /[<>]=?|[!=]=?=?|--?|\+\+?|&&?|\|\|?|[?*/~^%]/,
    // 'punctuation': /[{}[\];(),.:]/
  });

  PrismObject.languages.collie = PrismLanguagesCollie;
}

export default addColliePrismLanguageSupport;
