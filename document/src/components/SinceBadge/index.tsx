import { ReactNode } from "react";
import clsx from "clsx";

import styles from "./styles.module.css";

interface SinceBadgeProps {
  /**​
   * 必须指定的版本号
   */
  version: string;
  /**​
   * 前缀文字
   * @default "Since: "
   */
  prefix?: string;
  /**​
   * 尺寸规格
   * @default 'md'
   */
  size?: "sm" | "md" | "lg";
  /**​
   * 自定义类名
   */
  className?: string;
}

/**​
 * 版本标记组件
 *
 * 注意：避免将其放在大标题之后（会在网页 title 显示出来）
 *
 * @example
 * ```tsx
 * <SinceBadge version="v2.1.0" size="lg" />
 * ```
 */
function SinceBadge({
  version,
  prefix = "Since: ",
  size = "md",
  className,
}: SinceBadgeProps): ReactNode {
  return (
    <span
      className={clsx(
        styles.sinceBadge,
        styles[`sinceBadge--${size}`],
        className
      )}
      aria-label={`Introduced in version ${version}`}
    >
      {prefix}
      <span className={styles.version}>{version}</span>
    </span>
  );
}

export default SinceBadge;
