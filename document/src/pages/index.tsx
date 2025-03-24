import type { ReactNode } from "react";
import clsx from "clsx";
import Link from "@docusaurus/Link";
import useDocusaurusContext from "@docusaurus/useDocusaurusContext";
import Layout from "@theme/Layout";
import Heading from "@theme/Heading";
import Translate, { translate } from "@docusaurus/Translate";
import MDXContent from "@theme/MDXContent";

import styles from "./index.module.css";

function HomepageHeader() {
  const { siteConfig } = useDocusaurusContext();
  return (
    <header className={clsx("hero hero--primary", styles.heroBanner)}>
      <div className="container">
        <img
          src="img/logo.png"
          style={{
            width: "240px",
            height: "240px",
            margin: "15px",
            filter: "drop-shadow(4px 3px 2px black)",
          }}
        />
        <Heading as="h1" className="hero__title">
          {/* {siteConfig.title} */}
          <Translate
            id="homepage.header.title"
            description="Homepage header title"
          >
            Collie Lang Documentation
          </Translate>
        </Heading>
        <p className="hero__subtitle" style={{ fontStyle: "oblique" }}>
          {/* {siteConfig.tagline} */}
          <Translate
            id="homepage.header.tagline"
            description="Homepage header tagline"
          >
            The first step is always the hardest.
          </Translate>
        </p>
        <div className={styles.buttons}>
          <Link
            className="button button--secondary button--lg"
            to="/docs/tutorial/quick-start"
            style={buttonStyle}
          >
            <Translate
              id="homepage.header.button.quick-start"
              description="Homepage header [Quick Start] button"
            >
              Quick Start
            </Translate>
          </Link>
          <Link
            className="button button--primary button--lg"
            to="/docs/tutorial/intro"
            style={buttonStyle}
          >
            <Translate
              id="homepage.header.button.document"
              description="Homepage header [document] button"
            >
              Document
            </Translate>
          </Link>
        </div>
      </div>
    </header>
  );
}
const buttonStyle: React.CSSProperties = {
  marginRight: "15px",
};

export function HomepageContent() {
  const { i18n } = useDocusaurusContext();
  const locale = i18n.currentLocale;

  const IndexContent = require(`./_index-content.${locale}.md`).default;

  return (
    <div
      className="homepage-Content-Container"
      style={{
        width: "100%",
        maxWidth: "780px",
        // maxWidth: "960px",
        margin: "0 auto",
        padding: "32px 20px", // Browse on mobile
      }}
    >
      <MDXContent>
        <IndexContent />
      </MDXContent>
      <div style={{ textAlign: "center", marginTop: "24px" }}>
        <Link
          className="button button--primary button--lg"
          to="/docs/contribute/roadmap"
          style={buttonStyle}
        >
          <Translate
            id="homepage.header.button.roadmap"
            description="Homepage header [Roadmap] button"
          >
            Roadmap
          </Translate>
        </Link>
        <Link
          className="button button--secondary button--lg"
          to="/docs/contribute/work-progress"
          style={buttonStyle}
        >
          <Translate
            id="homepage.header.button.work-progress"
            description="Homepage header [Work Progress] button"
          >
            Work Progress
          </Translate>
        </Link>
      </div>
    </div>
  );
}

export default function Home(): ReactNode {
  const { siteConfig } = useDocusaurusContext();
  return (
    <Layout
      title={`Hello from ${siteConfig.title}`}
      description="Description will go into a meta tag in <head />"
    >
      <HomepageHeader />
      <HomepageContent />
    </Layout>
  );
}
