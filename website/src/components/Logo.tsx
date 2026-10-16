// The supplied NightMare Network mark is shared with the favicon.
export function Logo({ size = 32, className }: { size?: number; className?: string }) {
  return (
    <img
      src="/nightmare_logo.svg"
      className={className}
      width={size}
      height={size}
      alt=""
      aria-hidden="true"
    />
  );
}
