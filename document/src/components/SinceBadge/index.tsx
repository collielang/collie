import React from 'react';
import clsx from 'clsx';

import styles from './styles.module.css';

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
  size?: 'sm' | 'md' | 'lg';
  /**​
   * 自定义类名
   */
  className?: string;
}

/**​
 * 版本标记组件
 *
 * @example
 * ```tsx
 * <SinceBadge version="v2.1.0" size="lg" />
 * ```
 */
const SinceBadge: React.FC<SinceBadgeProps> = ({
  version,
  prefix = "Since: ",
  size = 'md',
  className
}) => {
  return (
    <span
      className={clsx(
        styles.badge,
        styles[`badge--${size}`],
        className
      )}
      aria-label={`Introduced in version ${version}`}
    >
      {prefix}
      <span className={styles.version}>{version}</span>
    </span>
  );
};

export default SinceBadge;
