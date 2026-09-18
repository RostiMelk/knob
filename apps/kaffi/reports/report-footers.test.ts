import { expect, test } from "vitest";
import { nextFooter } from "./report-footers.ts";

test("all 16 footers rotate randomly without repeats in any three consecutive images", () => {
  const footers = Array.from({ length: 1600 }, () => nextFooter());
  const firstCycle = new Set(footers.slice(0, 16));
  expect(firstCycle.size).toBe(16);
  for (let i = 0; i < footers.length; i += 16)
    expect(new Set(footers.slice(i, i + 16))).toEqual(firstCycle);
  for (let i = 2; i < footers.length; i++)
    expect(new Set(footers.slice(i - 2, i + 1)).size).toBe(3);
});
