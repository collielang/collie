Initialize the i18n folder

1. Initialize the JSON translation files:

```sh
npm run write-translations -- --locale zh-Hans
```

2. 把未翻译的 Markdown 文件复制到简体中文文件夹：

```sh
mkdir -p i18n/zh-Hans/docusaurus-plugin-content-docs/current
cp -r docs/** i18n/zh-Hans/docusaurus-plugin-content-docs/current

mkdir -p i18n/zh-Hans/docusaurus-plugin-content-blog
cp -r blog/** i18n/zh-Hans/docusaurus-plugin-content-blog

mkdir -p i18n/zh-Hans/docusaurus-plugin-content-pages
cp -r src/pages/**.md i18n/zh-Hans/docusaurus-plugin-content-pages
cp -r src/pages/**.mdx i18n/zh-Hans/docusaurus-plugin-content-pages
```
