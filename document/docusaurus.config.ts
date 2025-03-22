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
  baseUrl: '/CollieLang/',

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
            'https://github.com/CollieLang/CollieLang/tree/main/document/',
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
            'https://github.com/CollieLang/CollieLang/tree/main/document/',
          // Useful options to enforce blogging best practices
          onInlineTags: 'warn',
          onInlineAuthors: 'warn',
          onUntruncatedBlogPosts: 'warn',
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
          sidebarId: 'grammerSidebar',
          position: 'left',
          label: 'Grammer',
        },
        {
          type: 'docSidebar',
          sidebarId: 'contributeSidebar',
          position: 'left',
          label: 'Contribute',
        },
        {
          type: 'docSidebar',
          sidebarId: 'othersSidebar',
          position: 'left',
          label: '[DEBUG]',
        },
        {
          to: '/blog',
          label: 'Blog',
          position: 'left',
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
              href: 'https://github.com/CollieLang/CollieLang',
            },
            {
              label: 'Gitee',
              href: 'https://gitee.com/CollieLang/CollieLang',
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
              label: 'Grammer',
              to: '/docs/grammer/intro',
            },
            {
              label: 'Contribute',
              to: '/docs/contribute/intro',
            },
          ],
        },
        {
          title: 'Community',
          items: [
            {
              label: 'Github Discussions',
              href: 'https://github.com/CollieLang/CollieLang/discussions',
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
          title: 'More',
          items: [
            {
              label: 'Blog',
              to: '/blog',
            },
            {
              label: 'GitHub',
              href: 'https://github.com/CollieLang/CollieLang',
            },
            {
              label: 'Gitee',
              href: 'https://gitee.com/CollieLang/CollieLang',
            },
          ],
        },
      ],
      copyright: `Collie Lang`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
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
  themes: ['@docusaurus/theme-mermaid'],
};

export default config;
