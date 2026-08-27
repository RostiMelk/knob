import { RocketIcon } from "@sanity/icons/Rocket";
import { defineField, defineType } from "sanity";

// OTA delivery channel. CI writes this document with createOrReplace as
// _id "firmwareRelease", so liveEdit keeps it a single published document
// with no draft stage for devices to miss.
export const firmwareRelease = defineType({
  name: "firmwareRelease",
  title: "Firmware release",
  type: "document",
  icon: RocketIcon,
  liveEdit: true,
  fields: [
    defineField({
      name: "version",
      type: "string",
      description:
        'Semver, e.g. "1.2.0". Devices compare this against their running ' +
        "version and update when newer.",
      validation: (rule) => rule.required(),
    }),
    defineField({
      name: "bin",
      title: "Firmware binary",
      type: "file",
      description: "The kaffi.bin devices download and flash.",
    }),
    defineField({
      name: "notes",
      type: "text",
    }),
  ],
});
