import { themes as prismThemes } from 'prism-react-renderer';
import type { Config } from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

// This runs in Node.js - Don't use client-side code here (browser APIs, JSX...)

const config: Config = {
  title: 'Collie Lang',
  tagline: '"The first step is always the hardest."',
  favicon: 'img/favicon.ico',

  // Set the production url of your site here
  url: 'https://docs.example.com',
  // Set the /<baseUrl>/ pathname under which your site is served
  // For GitHub pages deployment, it is often '/<projectName>/'
  baseUrl: '/collie/',

  // GitHub pages deployment config.
  // If you aren't using GitHub pages, you don't need these.
  organizationName: 'Collie Lang', // Usually your GitHub org/user name.
  projectName: 'docusaurus', // Usually your repo name.

  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',

  // Even if you don't use internationalization, you can use this field to set
  // useful metadata like html lang. For example, if your site is Chinese, you
  // may want to replace "en" with "zh-Hans".
  i18n: {
    defaultLocale: 'en',
    locales: [
      // refer: https://docusaurus.io/zh-CN/docs/i18n/git
      'en',
      'zh-Hans',
    ],
  },

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          // Please change this to your repo.
          // Remove this to remove the "edit this page" links.
          editUrl:
            'https://github.com/collielang/collie/tree/main/document/',
          showLastUpdateTime: true,
          // The edit URL will target the localized file, instead of the original unlocalized file. Ignored when `editUrl` is a function.
          editLocalizedFiles: true,
          // sidebarCollapsed: false,
        },
        blog: {
          showReadingTime: true,
          feedOptions: {
            type: ['rss', 'atom'],
            xslt: true,
          },
          // Please change this to your repo.
          // Remove this to remove the "edit this page" links.
          editUrl:
            'https://github.com/collielang/collie/tree/main/document/',
          // Useful options to enforce blogging best practices
          onInlineTags: 'warn',
          onInlineAuthors: 'warn',
          onUntruncatedBlogPosts: 'warn',
          // The edit URL will target the localized file, instead of the original unlocalized file. Ignored when `editUrl` is a function.
          editLocalizedFiles: true,
        },
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    // Replace with your project's social card
    image: 'img/docusaurus-social-card.jpg',
    navbar: {
      title: 'Collie Lang',
      logo: {
        alt: 'Collie Lang Logo',
        src: 'img/logo.png',
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'tutorialSidebar',
          position: 'left',
          label: 'Tutorial',
        },
        {
          type: 'docSidebar',
          sidebarId: 'referenceSidebar',
          position: 'left',
          label: 'Reference',
        },
        {
          type: 'docSidebar',
          sidebarId: 'ecoSidebar',
          position: 'left',
          label: 'Eco',
        },
        {
          type: 'docSidebar',
          sidebarId: 'contributeSidebar',
          position: 'left',
          label: 'Contribute',
        },
        {
          to: '/blog',
          label: 'Blog',
          position: 'left',
        },
        {
          type: 'docSidebar',
          sidebarId: 'othersSidebar',
          position: 'left',
          label: '[DEBUG]',
        },

        {
          type: 'localeDropdown',
          position: 'right',
        },
        {
          type: 'dropdown',
          label: 'Open Source',
          position: 'right',
          items: [
            {
              label: 'GitHub',
              href: 'https://github.com/collielang/collie',
            },
            {
              label: 'Gitee',
              href: 'https://gitee.com/collielang/collie',
            },
          ],
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Docs',
          items: [
            {
              label: 'Tutorial',
              to: '/docs/tutorial/intro',
            },
            {
              label: 'Reference',
              to: '/docs/reference/intro',
            },
          ],
        },
        {
          title: 'Eco',
          items: [
            {
              label: 'CoUp - Package Manager',
              to: '/docs/eco/package-manager',
            },
            {
              label: 'CollObf - Code Obfuscation',
              to: '/docs/eco/code-obfuscation',
            },
            {
              html: `<hr style="margin: 10px 0; width: 80%; opacity: .2;">`
            },
            {
              label: 'Roadmap',
              to: '/docs/contribute/roadmap',
            },
          ],
        },
        {
          title: 'Community',
          items: [
            {
              label: 'Github Discussions',
              href: 'https://github.com/collielang/collie/discussions',
            },
            /*
            {
              label: 'Stack Overflow',
              href: 'https://stackoverflow.com/questions/tagged/docusaurus',
            },
            {
              label: 'Discord',
              href: 'https://discordapp.com/invite/docusaurus',
            },
            {
              label: 'X',
              href: 'https://x.com/docusaurus',
            },
            */
          ],
        },
        {
          title: 'Developer\'s Guide',
          items: [
            {
              label: 'Contribute',
              to: '/docs/contribute/intro',
            },
          ],
        },
        {
          title: 'More',
          items: [
            {
              label: 'Blog',
              to: '/blog',
            },
            {
              label: 'License',
              to: '/docs/contribute/license',
            },
          ],
        },
        {
          title: 'Open Source',
          items: [
            {
              label: 'GitHub',
              href: 'https://github.com/collielang/collie',
            },
            {
              label: 'Gitee',
              href: 'https://gitee.com/collielang/collie',
            },
          ],
        },
      ],
      logo: {
        alt: 'Logo',
        src: 'img/logo.png',
        // href: '',
        width: 120,
        height: 120,
        style: {
          filter: "drop-shadow(black 4px 3px 2px)",
        },
      },
      copyright: `Collie Lang`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
    },
    tableOfContents: {
      minHeadingLevel: 2,  // 最小显示的标题级别 (H2)
      maxHeadingLevel: 4,  // 最大显示的标题级别 (H4)
    },
    mermaid: {
      // refer: https://mermaid.js.org/config/theming.html
      theme: {
        light: 'neutral', // default
        dark: 'dark',
      },
    },
  } satisfies Preset.ThemeConfig,

  markdown: {
    mermaid: true,
  },
  themes: [
    '@docusaurus/theme-mermaid',
    // document local search
    // refer: https://github.com/easyops-cn/docusaurus-search-local
    [
      require.resolve("@easyops-cn/docusaurus-search-local"),
      /** @type {import("@easyops-cn/docusaurus-search-local").PluginOptions} */
      ({
        // ... Your options.
        // `hashed` is recommended as long-term-cache of index file is possible.
        hashed: true,

        // For Docs using Chinese, it is recomended to set:
        language: ["en", "zh"],

        // If you're using `noIndex: true`, set `forceIgnoreNoIndex` to enable local index:
        // forceIgnoreNoIndex: true,
      }),
    ],
  ],
};

export default config;
