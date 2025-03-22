import type { ReactNode } from "react";
import clsx from "clsx";
import Link from "@docusaurus/Link";
import useDocusaurusContext from "@docusaurus/useDocusaurusContext";
import Layout from "@theme/Layout";
import Heading from "@theme/Heading";
import Translate, { translate } from "@docusaurus/Translate";

import styles from "./index.module.css";

function HomepageHeader() {
  const { siteConfig } = useDocusaurusContext();
  return (
    <header className={clsx("hero hero--primary", styles.heroBanner)}>
      <div className="container">
        <img src="img/logo.png" style={{ width: "240px", margin: "15px" }} />
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
            style={{ marginRight: "15px" }}
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

export default function Home(): ReactNode {
  const { siteConfig } = useDocusaurusContext();
  return (
    <Layout
      title={`Hello from ${siteConfig.title}`}
      description="Description will go into a meta tag in <head />"
    >
      <HomepageHeader />
    </Layout>
  );
}
