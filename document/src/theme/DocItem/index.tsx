import React, { type ReactNode } from "react";
import clsx from "clsx";
import DocItem from "@theme-original/DocItem";
import type DocItemType from "@theme/DocItem";
import type { WrapperProps } from "@docusaurus/types";
import Admonition from "@theme/Admonition";

type Props = WrapperProps<typeof DocItemType>;

// npm run swizzle @docusaurus/theme-classic DocItem

export default function DocItemWrapper(props: Props): ReactNode {
  return (
    <>
      {/* 文章段前提示 */}
      <div
        style={{
          maxWidth: "max(75%, 810px)",
          marginTop: "15px",
          marginBottom: "32px",
          marginLeft: "auto",
          marginRight: "auto",
        }}
      >
        <Admonition type="danger" title="🚨 重要提示">
          <p>
            本项目目前仍处于早期开发阶段，核心功能尚未完全实现，语法规范和工具链仍在持续完善中。
          </p>
        </Admonition>
      </div>
      <DocItem {...props} />
    </>
  );
}
