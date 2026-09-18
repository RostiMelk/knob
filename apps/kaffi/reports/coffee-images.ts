import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { Resvg } from "@resvg/resvg-js";
import { FIRST_POT_CUPS, EXTRA_POT_CUPS } from "../lib/coffee-stats.ts";
import type { CoffeeReport } from "./report-data.ts";

const WIDTH = 1200;
const HEIGHT = 800;
const INK = "#0b0b0b";
const ORANGE = "#ff5500";
const YELLOW = "#ffff00";
const WHITE = "#ffffff";
const FONT = "KMR Waldenburg";
const MONO = "IBM Plex Mono";
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
const BALANCE_RULE = `${FIRST_POT_CUPS} credits for the first pot + ${EXTRA_POT_CUPS} per extra pot in a brew, minus cups taken.`;

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
  } = {},
) {
  const family = options.mono ? MONO : FONT;
  let label = String(value);
  const measure = (content: string) => textWidth(content, size, family);
  if (options.width) {
    const measured = measure(label);
    if (measured > options.width) {
      size = Math.max(Math.min(size, 24), (size * options.width) / measured);
      if (measure(label) > options.width) {
        const characters = Array.from(label);
        let low = 0,
          high = characters.length;
        while (low < high) {
          const middle = Math.ceil((low + high) / 2);
          if (
            measure(characters.slice(0, middle).join("") + "…") <= options.width
          )
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
  const widths = words.map((word) => textWidth(word, 78, FONT));
  const gap =
    (1120 - widths.reduce((sum, width) => sum + width, 0)) / (words.length - 1);
  let x = 40;
  return words
    .map((word, i) => {
      const label = text(word, x, 176, 78);
      x += widths[i];
      const spacer =
        i < words.length - 1
          ? rect(x + 24, 120, gap - 48, 56, "url(#dots)")
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
    <defs><pattern id="dots" width="12" height="12" patternUnits="userSpaceOnUse"><circle cx="1.5" cy="1.5" r="1.5" fill="${INK}"/><circle cx="7.5" cy="7.5" r="1.5" fill="${INK}"/></pattern></defs>
    ${rect(0, 0, WIDTH, HEIGHT, background)}
    <g transform="translate(40 31) scale(2.3)">${symbol}</g>
    ${text("SANITY / KAFFI", 92, 56, 19, { mono: true })}
    ${text(report.office.toUpperCase(), 1160, 56, 19, { mono: true, align: "end", width: 560 })}
    ${rule(82)}
    ${headline(title)}
    ${text(`${report.periodLabel}${report.toDate ? " TO DATE" : ""} / ${report.dateLabel.toUpperCase()}`, 40, 222, 19, { mono: true })}
    ${content}
    ${rule(741)}
    ${text("BREWED BY PEOPLE. COUNTED BY SANITY.", 40, 775, 17, { mono: true })}
    ${text("SANITY.IO", 1160, 775, 17, { mono: true, align: "end" })}
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
        tied ? "LEADING BREWER / TIED FOR FIRST" : "THE PERSON BEHIND THE POTS",
        40,
        474,
        18,
        { mono: true },
      ) +
      text(top.name, 40, 565, 64, { width: 790 }) +
      text(
        `${number.format(top.pots)} ${top.pots === 1 ? "pot" : "pots"} brewed / ${number.format(top.cups)} ${top.cups === 1 ? "cup" : "cups"} taken`,
        40,
        614,
        22,
        { mono: true, width: 780 },
      ) +
      text(signed(top.balance), 1160, 565, 84, {
        align: "end",
        width: 210,
      }) +
      text("BALANCE", 1160, 612, 19, { mono: true, align: "end" })
    : text("EVERY GOOD DAY STARTS WITH A POT.", 40, 476, 18, {
        mono: true,
      }) +
      text(
        report.participants ? "Who's brewing next?" : "A fresh start.",
        40,
        565,
        64,
        { width: 1000 },
      ) +
      text(
        report.participants
          ? "Cups were logged, but no brews yet."
          : "No coffee activity recorded for this period.",
        40,
        620,
        22,
      );
  return frame(
    report,
    ORANGE,
    "THE KAFFI REPORT",
    stats
      .map(
        ([value, label], i) =>
          text(number.format(value), 40 + i * 284, 350, 94, { width: 248 }) +
          text(label, 40 + i * 284, 391, 18, { mono: true }),
      )
      .join("") +
      rule(430) +
      highlight +
      text(BALANCE_RULE, 40, 682, 16, {
        mono: true,
        width: 1060,
      }),
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
      text(number.format(i * step), left - 18, baseline + 6, 17, {
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
      const labelY =
        Math.abs(y(last[key]) - y(last[other])) < 34
          ? y(last[key]) + (key === "cups" ? -17 : 17)
          : y(last[key]);
      return (
        segments +
        dots +
        text(
          `${number.format(last[key])} ${key.toUpperCase()}${last.partial ? "*" : ""}`,
          left + width + 24,
          labelY + 6,
          18,
          { mono: true, color, width: 148 },
        )
      );
    })
    .join("");
  const labels = points
    .map((point, i) =>
      i % labelEvery === 0 || i === points.length - 1
        ? text(
            point.label + (point.partial ? "*" : ""),
            point.x,
            bottom + 30,
            16,
            { mono: true, align: "middle" },
          )
        : "",
    )
    .join("");
  const legend =
    rect(40, 270, 28, 3, INK) +
    text("POTS BREWED", 80, 279, 18, { mono: true }) +
    rect(282, 270, 28, 3, ORANGE) +
    text("CUPS TAKEN", 322, 279, 18, { mono: true }) +
    text("LOGGED COUNTS", 1160, 279, 17, { mono: true, align: "end" });
  const daily = report.period === "week" || report.period === "month";
  const data = hasActivity
    ? lines
    : text(
        daily ? "No weekday activity recorded" : "No activity recorded",
        624,
        476,
        30,
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
        15,
        { mono: true },
      ) +
      (report.buckets.some((bucket) => bucket.partial)
        ? text("* PARTIAL WEEK", 1160, 720, 15, { mono: true, align: "end" })
        : ""),
  );
}

/** Top five participants, ranked by balance, then pots. Equal scores share a rank. */
export function renderCoffeeLeaderboard(report: CoffeeReport): Buffer {
  let rank = 1;
  const rows = report.standings
    .slice(0, 5)
    .map((person, i) => {
      const previous = report.standings[i - 1];
      if (
        previous &&
        (previous.balance !== person.balance || previous.pots !== person.pots)
      )
        rank = i + 1;
      const y = 327 + i * 70;
      return (
        (i === 0 ? rect(40, y - 39, 1120, 64, INK) : "") +
        text(String(rank).padStart(2, "0"), 62, y + 4, 25, {
          mono: true,
          color: i === 0 ? YELLOW : INK,
        }) +
        text(person.name, 135, y + 4, 30, {
          color: i === 0 ? WHITE : INK,
          width: 590,
        }) +
        text(number.format(person.pots), 835, y + 4, 27, {
          mono: true,
          color: i === 0 ? WHITE : INK,
          align: "end",
          width: 100,
        }) +
        text(number.format(person.cups), 975, y + 4, 27, {
          mono: true,
          color: i === 0 ? WHITE : INK,
          align: "end",
          width: 100,
        }) +
        text(signed(person.balance), 1136, y + 4, 30, {
          mono: true,
          color: i === 0 ? YELLOW : INK,
          align: "end",
          width: 140,
        }) +
        (i > 0 ? rule(y + 25) : "")
      );
    })
    .join("");
  const headings =
    text("RANK / PERSON", 62, 272, 17, { mono: true }) +
    [
      { title: "POTS", x: 835 },
      { title: "CUPS", x: 975 },
      { title: "BALANCE", x: 1136 },
    ]
      .map(({ title, x }) =>
        text(title, x, 272, 17, { mono: true, align: "end" }),
      )
      .join("");
  const note =
    report.participants > 5
      ? `Top 5 of ${report.participants} participants. `
      : "";
  return frame(
    report,
    YELLOW,
    "KAFFI HEROES",
    headings +
      rows +
      (report.participants
        ? ""
        : text("No heroes on the board yet. Brew the first pot.", 62, 400, 34, {
            width: 1060,
          })) +
      text(
        note +
          "Ranked by balance, then pots brewed. Equal scores share a rank.",
        40,
        682,
        17,
        { mono: true, width: 1120 },
      ) +
      text(BALANCE_RULE, 40, 713, 16, { mono: true, width: 1120 }),
  );
}
