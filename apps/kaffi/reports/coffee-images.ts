import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { Resvg } from "@resvg/resvg-js";
import type { CoffeeReport } from "./report-data.ts";
import { nextFooter } from "./report-footers.ts";

const WIDTH = 1200;
const HEIGHT = 800;
const INK = "#0b0b0b";
const ORANGE = "#ff5500";
const YELLOW = "#ffff00";
const WHITE = "#ffffff";
const FONT = "KMR Waldenburg";
const MONO = "IBM Plex Mono";
const TYPE = { headline: 78, value: 64, body: 32, label: 18 };
const DOT_TILE = 12;
const font = {
  loadSystemFonts: false,
  fontFiles: ["waldenburg-normal.ttf", "ibm-plex-mono-regular.ttf"].map(
    (name) => fileURLToPath(new URL(`./assets/${name}`, import.meta.url)),
  ),
  defaultFontFamily: FONT,
};
const symbol = readFileSync(
  new URL("./assets/sanity-symbol.svg", import.meta.url),
  "utf8",
);
const number = new Intl.NumberFormat("en-US");

function escapeXml(value: string | number) {
  return String(value)
    .replace(/[&<>"']/g, (char) => {
      switch (char) {
        case "&":
          return "&amp;";
        case "<":
          return "&lt;";
        case ">":
          return "&gt;";
        case '"':
          return "&quot;";
        default:
          return "&apos;";
      }
    })
    .replace(/[\x00-\x08\x0b\x0c\x0e-\x1f]/g, "");
}

function textWidth(content: string, size: number, family: string) {
  const bounds = new Resvg(
    `<svg xmlns="http://www.w3.org/2000/svg" width="${Math.max(1, content.length * size * 2)}" height="${size * 3}"><text x="0" y="${size * 2}" font-family="${family}" font-size="${size}">${escapeXml(content)}</text></svg>`,
    { font },
  ).innerBBox();
  return bounds ? Math.max(0, bounds.x) + bounds.width : 0;
}

function text(
  value: string | number,
  x: number,
  y: number,
  size: number,
  options: {
    color?: string;
    mono?: boolean;
    align?: "start" | "end" | "middle";
    width?: number;
    truncate?: boolean;
  } = {},
) {
  const family = options.mono ? MONO : FONT;
  let label = String(value);
  const measure = (content: string) => textWidth(content, size, family);
  const width = options.width;
  if (width) {
    const measured = measure(label);
    if (measured > width) {
      if (!options.truncate)
        size =
          Object.values(TYPE).find(
            (candidate) =>
              candidate <= size && textWidth(label, candidate, family) <= width,
          ) ?? TYPE.label;
      if (measure(label) > width) {
        const characters = Array.from(label);
        let low = 0,
          high = characters.length;
        while (low < high) {
          const middle = Math.ceil((low + high) / 2);
          if (measure(characters.slice(0, middle).join("") + "…") <= width)
            low = middle;
          else high = middle - 1;
        }
        label = characters.slice(0, low).join("") + "…";
      }
    }
  }
  return `<text x="${x}" y="${y}" font-family="${family}" font-size="${size}" fill="${options.color ?? INK}" text-anchor="${options.align ?? "start"}">${escapeXml(label)}</text>`;
}

function rect(
  x: number,
  y: number,
  width: number,
  height: number,
  fill: string,
) {
  return `<rect x="${x}" y="${y}" width="${width}" height="${height}" fill="${fill}"/>`;
}

function rule(y: number, color = INK) {
  return rect(40, y, 1120, 1, color);
}
function signed(value: number) {
  return `${value > 0 ? "+" : ""}${number.format(value)}`;
}

function headline(title: string) {
  const words = title.split(" ");
  const widths = words.map((word) => textWidth(word, TYPE.headline, FONT));
  const gap =
    (1120 - widths.reduce((sum, width) => sum + width, 0)) / (words.length - 1);
  const textureWidth = Math.max(
    0,
    Math.floor((gap - 24) / DOT_TILE) * DOT_TILE,
  );
  let x = 40;
  return words
    .map((word, i) => {
      const label = text(word, x, 176, TYPE.headline);
      x += widths[i];
      const spacer =
        i < words.length - 1 && textureWidth > 0
          ? `<g transform="translate(${x + (gap - textureWidth) / 2} 118)">${rect(0, 0, textureWidth, DOT_TILE * 5, "url(#dots)")}</g>`
          : "";
      x += gap;
      return label + spacer;
    })
    .join("");
}

function frame(
  report: CoffeeReport,
  background: string,
  title: string,
  content: string,
) {
  // Hex-dot geometry and symbol match www-sanity-io's engineering OG assets.
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${WIDTH}" height="${HEIGHT}" viewBox="0 0 ${WIDTH} ${HEIGHT}">
    <title>${escapeXml(`${title}: ${report.office}, ${report.dateLabel}`)}</title>
    <defs><pattern id="dots" width="${DOT_TILE}" height="${DOT_TILE}" patternUnits="userSpaceOnUse"><circle cx="3" cy="3" r="1.5" fill="${INK}"/><circle cx="9" cy="9" r="1.5" fill="${INK}"/></pattern></defs>
    ${rect(0, 0, WIDTH, HEIGHT, background)}
    <g transform="translate(40 31) scale(2.3)">${symbol}</g>
    ${text("SANITY / KAFFI", 92, 56, TYPE.label, { mono: true })}
    ${text(report.office.toUpperCase(), 1160, 56, TYPE.label, { mono: true, align: "end", width: 560 })}
    ${rule(82)}
    ${headline(title)}
    ${text(`${report.periodLabel}${report.toDate ? " TO DATE" : ""} / ${report.dateLabel.toUpperCase()}`, 40, 222, TYPE.label, { mono: true })}
    ${content}
    ${rule(741)}
    ${text(nextFooter().toUpperCase(), 40, 775, TYPE.label, { mono: true, width: 980 })}
    ${text("SANITY.IO", 1160, 775, TYPE.label, { mono: true, align: "end" })}
  </svg>`;
  return new Resvg(svg, { font }).render().asPng();
}

/** A 1200×800 PNG Buffer. No network requests or file writes. */
export function renderCoffeeSummary(report: CoffeeReport): Buffer {
  const stats = [
    [report.pots, "POTS BREWED"],
    [report.cups, "CUPS TAKEN"],
    [report.brewers, "COFFEE MAKERS"],
    [report.participants, "PARTICIPANTS"],
  ] as const;
  const top = report.standings.find((person) => person.pots > 0);
  const tied =
    top &&
    report.standings.some(
      (person) =>
        person.personId !== top.personId &&
        person.pots > 0 &&
        person.balance === top.balance &&
        person.pots === top.pots,
    );
  const highlight = top
    ? text(
        tied ? "LEADING BREWER / TIED FOR FIRST" : "LEADING BREWER",
        40,
        474,
        TYPE.label,
        { mono: true },
      ) +
      text(top.name, 40, 555, TYPE.value, { width: 1120, truncate: true }) +
      [
        [number.format(top.pots), "POTS BREWED"],
        [number.format(top.cups), "CUPS TAKEN"],
        [signed(top.balance), "BALANCE"],
      ]
        .map(
          ([value, label], i) =>
            text(value, 40 + i * 284, 644, TYPE.value, { width: 248 }) +
            text(label, 40 + i * 284, 684, TYPE.label, { mono: true }),
        )
        .join("")
    : text("EVERY GOOD DAY STARTS WITH A POT.", 40, 476, TYPE.label, {
        mono: true,
      }) +
      text(
        report.participants ? "Who's brewing next?" : "A fresh start.",
        40,
        565,
        TYPE.value,
        { width: 1000 },
      ) +
      text(
        report.participants
          ? "Cups were logged, but no brews yet."
          : "No coffee activity recorded for this period.",
        40,
        620,
        TYPE.label,
        { mono: true },
      );
  return frame(
    report,
    ORANGE,
    "THE KAFFI REPORT",
    stats
      .map(
        ([value, label], i) =>
          text(number.format(value), 40 + i * 284, 350, TYPE.value, {
            width: 248,
          }) + text(label, 40 + i * 284, 391, TYPE.label, { mono: true }),
      )
      .join("") +
      rule(430) +
      highlight,
  );
}

/** Logged pots and cups use the same zero-based count axis. */
export function renderCoffeeTrend(report: CoffeeReport): Buffer {
  const max = Math.max(
    1,
    ...report.buckets.flatMap((bucket) => [bucket.pots, bucket.cups]),
  );
  const magnitude = 10 ** Math.floor(Math.log10(max / 4));
  const step = Math.max(1, Math.ceil(max / 4 / magnitude) * magnitude);
  const ceiling = step * 4;
  const left = 104,
    bottom = 650,
    height = 328,
    width = 880;
  const labelEvery = Math.ceil(report.buckets.length / 10);
  const points = report.buckets.map((bucket, i) => ({
    ...bucket,
    x:
      left +
      (report.buckets.length === 1
        ? width / 2
        : (i * width) / (report.buckets.length - 1)),
  }));
  const y = (value: number) => bottom - (value / ceiling) * height;
  const hasActivity = points.some((point) => point.pots || point.cups);
  const grid = Array.from({ length: 5 }, (_, i) => {
    const baseline = bottom - (i * height) / 4;
    return (
      rect(left, baseline, width, 1, "#d6d6d6") +
      text(number.format(i * step), left - 18, baseline + 6, TYPE.label, {
        mono: true,
        align: "end",
        width: 68,
      })
    );
  }).join("");
  const lines = (
    [
      { key: "pots", color: INK },
      { key: "cups", color: ORANGE },
    ] as const
  )
    .map(({ key, color }) => {
      const segments = points
        .slice(1)
        .map((point, i) => {
          const previous = points[i];
          return `<path d="M${previous.x} ${y(previous[key])} L${point.x} ${y(point[key])}" fill="none" stroke="${color}" stroke-width="3" stroke-linecap="round"${point.partial || previous.partial ? ' stroke-dasharray="7 8"' : ""}/>`;
        })
        .join("");
      const dots = points
        .map(
          (point) =>
            `<circle cx="${point.x}" cy="${y(point[key])}" r="${point.partial ? 6 : 4.5}" fill="${point.partial ? WHITE : color}" stroke="${point.partial ? color : WHITE}" stroke-width="${point.partial ? 2.5 : 1.5}"/>`,
        )
        .join("");
      const last = points.at(-1);
      if (!last) return "";
      const other = key === "pots" ? "cups" : "pots";
      const labelCenter = Math.min(
        bottom - TYPE.label,
        Math.max(
          bottom - height + TYPE.label,
          (y(last.pots) + y(last.cups)) / 2,
        ),
      );
      const labelY =
        Math.abs(y(last[key]) - y(last[other])) < TYPE.label * 2
          ? labelCenter + (key === "cups" ? -TYPE.label : TYPE.label)
          : y(last[key]);
      return (
        segments +
        dots +
        text(
          `${number.format(last[key])} ${key.toUpperCase()}${last.partial ? "*" : ""}`,
          left + width + 24,
          labelY + 6,
          TYPE.label,
          { mono: true, color, width: 148 },
        )
      );
    })
    .join("");
  const labels = points
    .map((point, i) =>
      i === points.length - 1 ||
      (i % labelEvery === 0 && points.length - 1 - i >= labelEvery)
        ? text(
            point.label + (point.partial ? "*" : ""),
            point.x,
            bottom + 30,
            TYPE.label,
            { mono: true, align: "middle" },
          )
        : "",
    )
    .join("");
  const legend =
    rect(40, 270, 28, 3, INK) +
    text("POTS BREWED", 80, 279, TYPE.label, { mono: true }) +
    rect(282, 270, 28, 3, ORANGE) +
    text("CUPS TAKEN", 322, 279, TYPE.label, { mono: true }) +
    text("LOGGED COUNTS", 1160, 279, TYPE.label, { mono: true, align: "end" });
  const daily = report.period === "week" || report.period === "month";
  const data = hasActivity
    ? lines
    : text(
        daily ? "No weekday activity recorded" : "No activity recorded",
        624,
        476,
        TYPE.body,
        { align: "middle" },
      );
  return frame(
    report,
    WHITE,
    "BREW. SIP. REPEAT.",
    legend +
      grid +
      data +
      labels +
      text(
        `${report.timeZone} / ${daily ? "Weekdays only" : report.period === "quarter" ? "Weekly totals" : "Monthly totals"}`,
        40,
        720,
        TYPE.label,
        { mono: true },
      ) +
      (report.buckets.some((bucket) => bucket.partial)
        ? text("* PARTIAL WEEK", 1160, 720, TYPE.label, {
            mono: true,
            align: "end",
          })
        : ""),
  );
}

function standingCells(
  person: CoffeeReport["standings"][number],
  y: number,
  highlight = false,
): string {
  return (
    text(person.name, 135, y, TYPE.body, {
      color: highlight ? WHITE : INK,
      width: 590,
      truncate: true,
    }) +
    text(number.format(person.pots), 835, y, TYPE.body, {
      mono: true,
      color: highlight ? WHITE : INK,
      align: "end",
      width: 100,
    }) +
    text(number.format(person.cups), 975, y, TYPE.body, {
      mono: true,
      color: highlight ? WHITE : INK,
      align: "end",
      width: 100,
    }) +
    text(signed(person.balance), 1136, y, TYPE.body, {
      mono: true,
      color: highlight ? YELLOW : INK,
      align: "end",
      width: 140,
    })
  );
}

/** Top five and last participant, ranked by balance, then pots. Ties share a rank. */
export function renderCoffeeLeaderboard(report: CoffeeReport): Buffer {
  let rank = 1;
  const rows = report.standings
    .map((person, i) => {
      const previous = report.standings[i - 1];
      if (
        previous &&
        (previous.balance !== person.balance || previous.pots !== person.pots)
      )
        rank = i + 1;
      if (i >= 5 && i !== report.standings.length - 1) return "";
      const y = 327 + Math.min(i, 5) * 68;
      return (
        (i > 5
          ? `<path d="M40 ${y - 43} ${"l8 4 8 -4 ".repeat(70)}" fill="none" stroke="${INK}" stroke-width="1"/>`
          : i > 1
            ? rule(y - 43)
            : "") +
        (i === 0 ? rect(40, y - 39, 1120, 64, INK) : "") +
        text(String(rank).padStart(2, "0"), 62, y + 4, TYPE.body, {
          mono: true,
          color: i === 0 ? YELLOW : INK,
        }) +
        standingCells(person, y + 4, i === 0)
      );
    })
    .join("");
  const headings =
    text("RANK / PERSON", 40, 272, TYPE.label, { mono: true }) +
    [
      { title: "POTS", x: 835 },
      { title: "CUPS", x: 975 },
      { title: "BALANCE", x: 1160 },
    ]
      .map(({ title, x }) =>
        text(title, x, 272, TYPE.label, { mono: true, align: "end" }),
      )
      .join("");
  return frame(
    report,
    YELLOW,
    "KAFFI HEROES",
    text(`${report.participants} PARTICIPANTS`, 1160, 222, TYPE.label, {
      mono: true,
      align: "end",
    }) +
      headings +
      rows +
      (report.participants
        ? ""
        : text(
            "No heroes on the board yet. Brew the first pot.",
            62,
            400,
            TYPE.body,
            {
              width: 1060,
            },
          )),
  );
}
