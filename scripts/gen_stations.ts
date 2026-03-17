import { $ } from "bun";
import { existsSync, mkdirSync } from "fs";
import { join, resolve } from "path";

const ROOT = resolve(import.meta.dir, "..");
const STATIONS_JSON = join(ROOT, "stations.json");
const LOGO_DIR = join(ROOT, "assets", "logos");
const APP_CONFIG = join(ROOT, "main", "app_config.h");

const LOGO_SIZE = 120;

interface Station {
  id: string;
  name: string;
  stream_url: string;
  logo_url: string;
  color: string;
}

function ensureDirs() {
  for (const dir of [LOGO_DIR]) {
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
  for (const station of stations) {
    await downloadLogo(station);
  }

  if (!skipCpp) {
    console.log("\nUpdating app_config.h...");
    await patchAppConfig(stations);
  }

  console.log("\nDone ✓");
  console.log(`  Logos: ${LOGO_DIR}/`);
  console.log(
    `\nTo add a station: edit stations.json, run: bun scripts/gen_stations.ts`,
  );
}

main().catch(console.error);
