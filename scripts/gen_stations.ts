import { $ } from "bun";
import { existsSync, mkdirSync } from "fs";
import { join, resolve } from "path";

const ROOT = resolve(import.meta.dir, "..");
const STATIONS_JSON = join(ROOT, "stations.json");
const LOGO_DIR = join(ROOT, "assets", "logos");
const BG_DIR = join(ROOT, "assets", "logos", "bg");
const APP_CONFIG = join(ROOT, "main", "app_config.h");

const LOGO_SIZE = 100;
const BG_SIZE = 360;
const BG_BLUR_SIGMA = 90;
const BG_DEFAULT_MODULATE = "40,130,100";
const BG_DARK_MODULATE = "80,200,100";
const BG_DEFAULT_BC = "-10x10";
const BG_DARK_BC = "5x15";
const DARK_THRESHOLD = 65; // dominant brightness below this = "dark" source

interface Station {
  id: string;
  name: string;
  stream_url: string;
  logo_url: string;
  color: string;
}

function ensureDirs() {
  for (const dir of [LOGO_DIR, BG_DIR]) {
    if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
  }
}

async function downloadLogo(station: Station): Promise<string> {
  const dest = join(LOGO_DIR, `${station.id}.png`);

  if (existsSync(dest)) {
    console.log(`  ✓ ${station.id}.png (cached)`);
    return dest;
  }

  console.log(`  ↓ ${station.id}.png`);
  const res = await fetch(station.logo_url);
  if (!res.ok) {
    console.error(`  ✗ Failed to download ${station.logo_url}: ${res.status}`);
    return dest;
  }

  const tmp = `${dest}.tmp`;
  await Bun.write(tmp, await res.arrayBuffer());
  await $`magick ${tmp} -resize ${LOGO_SIZE}x${LOGO_SIZE} -background none -gravity center -extent ${LOGO_SIZE}x${LOGO_SIZE} PNG32:${dest}`.quiet();
  await $`rm -f ${tmp}`.quiet();

  return dest;
}

async function getDominantBrightness(path: string): Promise<number> {
  // Resize to 1x1 first — gives area-weighted dominant color, ignores small text/details
  const result =
    await $`magick ${path} -resize 1x1! -colorspace Gray -format "%[fx:mean*100]" info:`.quiet();
  return parseFloat(result.text().trim());
}

async function generateBackground(station: Station, logoPath: string) {
  const dest = join(BG_DIR, `${station.id}_bg.png`);
  const brightness = await getDominantBrightness(logoPath);
  const isDark = brightness < DARK_THRESHOLD;

  const modulate = isDark ? BG_DARK_MODULATE : BG_DEFAULT_MODULATE;
  const bc = isDark ? BG_DARK_BC : BG_DEFAULT_BC;

  if (isDark) {
    console.log(
      `  ◐ ${station.id}_bg.png (dark source: ${brightness.toFixed(1)}%, boosted)`,
    );
  } else {
    console.log(
      `  ◑ ${station.id}_bg.png (brightness: ${brightness.toFixed(1)}%)`,
    );
  }

  await $`magick ${logoPath} -resize ${BG_SIZE}x${BG_SIZE}! -blur 0x${BG_BLUR_SIGMA} -modulate ${modulate} -brightness-contrast ${bc} PNG24:${dest}`.quiet();
}

function generateCppArray(stations: Station[]): string {
  const entries = stations
    .map((s) => {
      const logo = `${s.id}.png`;
      return `    {"${s.name}",\n     "${s.stream_url}",\n     "${logo}", ${s.color}}`;
    })
    .join(",\n");

  return `constexpr Station STATIONS[] = {\n${entries},\n};\n\nconstexpr int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);`;
}

async function patchAppConfig(stations: Station[]) {
  const content = await Bun.file(APP_CONFIG).text();
  const cppArray = generateCppArray(stations);

  const startMarker = "constexpr Station STATIONS[]";
  const endMarker = "constexpr int STATION_COUNT";

  const lines = content.split("\n");
  const startIdx = lines.findIndex((l) => l.includes(startMarker));
  const endIdx = lines.findIndex(
    (l, i) => i > startIdx && l.includes(endMarker),
  );

  if (startIdx === -1 || endIdx === -1) {
    console.error("  ✗ Could not find STATIONS array markers in app_config.h");
    console.log("\n  Paste this into app_config.h manually:\n");
    console.log(cppArray);
    return;
  }

  const endOfEndLine =
    lines.findIndex((l, i) => i >= endIdx && l.includes(";")) + 1;
  const newLines = [
    ...lines.slice(0, startIdx),
    ...cppArray.split("\n"),
    ...lines.slice(endOfEndLine),
  ];

  await Bun.write(APP_CONFIG, newLines.join("\n"));
  console.log(`  ✓ app_config.h updated (${stations.length} stations)`);
}

async function main() {
  const flags = new Set(process.argv.slice(2));
  const forceDownload = flags.has("--force") || flags.has("-f");
  const skipCpp = flags.has("--no-cpp");

  if (forceDownload) {
    console.log("Force mode: re-downloading all logos\n");
    await $`rm -f ${LOGO_DIR}/*.png`.quiet().nothrow();
  }

  const stations: Station[] = await Bun.file(STATIONS_JSON).json();
  console.log(`Found ${stations.length} stations in stations.json\n`);

  ensureDirs();

  console.log("Downloading logos...");
  const logoPaths: string[] = [];
  for (const station of stations) {
    logoPaths.push(await downloadLogo(station));
  }

  console.log("\nGenerating blurred backgrounds...");
  for (let i = 0; i < stations.length; i++) {
    await generateBackground(stations[i], logoPaths[i]);
  }

  if (!skipCpp) {
    console.log("\nUpdating app_config.h...");
    await patchAppConfig(stations);
  }

  console.log("\nDone ✓");
  console.log(`  Logos:       ${LOGO_DIR}/`);
  console.log(`  Backgrounds: ${BG_DIR}/`);
  console.log(
    `\nTo add a station: edit stations.json, run: bun scripts/gen_stations.ts`,
  );
}

main().catch(console.error);
