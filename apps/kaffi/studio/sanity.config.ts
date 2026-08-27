import { DropIcon } from "@sanity/icons/Drop";
import { StarIcon } from "@sanity/icons/Star";
import { visionTool } from "@sanity/vision";
import { defineConfig } from "sanity";
import { structureTool } from "sanity/structure";

import { LeaderboardTool } from "./components/LeaderboardTool";
import { schemaTypes } from "./schemas/schemaTypes";

const projectId = process.env.SANITY_STUDIO_PROJECT_ID || "j7764jbe";
const dataset = process.env.SANITY_STUDIO_DATASET || "production";

export default defineConfig({
  name: "kaffi",
  title: "Kaffi",
  icon: DropIcon,
  projectId,
  dataset,
  plugins: [structureTool(), visionTool()],
  tools: [
    {
      name: "leaderboard",
      title: "Leaderboard",
      icon: StarIcon,
      component: LeaderboardTool,
    },
  ],
  schema: {
    types: schemaTypes,
  },
});
