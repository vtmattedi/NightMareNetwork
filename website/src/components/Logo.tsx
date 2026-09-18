// The mark from branding/logo_image_only.svg, inlined so the dark half follows
// the text colour: the source SVG draws it black, which vanishes on a dark page.

export function Logo({ size = 32, className }: { size?: number; className?: string }) {
  return (
    <svg
      className={className}
      width={size}
      height={size}
      viewBox="0 0 1080 1080"
      role="img"
      aria-label="NightMare Network"
    >
      <path
        fill="var(--brand)"
        d="M962.57,128.62c-35.96-43.38-101.53-42.47-136.5-.4-34.65,41.68-24.74,105.9,24.95,134.74l-.25,495.72-218.58-217.98-217.27,216.74-1.27-165.12-85.17-83.81.63,456.14,303.78-303.27,303.2,302.54.28-700.49c51.13-30.58,61.17-92.6,26.19-134.79ZM895.54,228.34c-25.18,0-45.58-20.41-45.58-45.58s20.41-45.58,45.58-45.58,45.58,20.41,45.58,45.58-20.41,45.58-45.58,45.58Z"
      />
      <path
        fill="currentColor"
        d="M117.43,951.38c35.96,43.38,101.53,42.47,136.5.4,34.65-41.68,24.74-105.9-24.95-134.74l.25-495.72,218.58,217.98,217.27-216.74,1.27,165.12,85.17,83.81-.63-456.14-303.78,303.27L143.91,116.1l-.28,700.49c-51.13,30.58-61.17,92.6-26.19,134.79Z"
      />
    </svg>
  );
}
