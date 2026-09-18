const FOOTERS = [
  "Be the colleague who makes the next pot.",
  "Make someone else’s coffee break.",
  "A fresh pot goes a long way.",
  "Small gesture. Better day for everyone.",
  "Leave some coffee for the next person. Better yet, make some.",
  "Good colleagues put another pot on.",
  "The office coffee report.",
  "Fresh pots. Fresh stats.",
  "A little credit for making coffee.",
  "For everyone who puts a pot on.",
  "Thanks for making the next pot.",
  "Someone made that coffee.",
  "Your next pot could change the rankings.",
  "Good coffee is a team effort.",
  "Keep the coffee coming.",
  "The numbers are in. The coffee’s on.",
];

let remaining = [...FOOTERS];
let recent: string[] = [];

export function nextFooter() {
  if (!remaining.length) remaining = [...FOOTERS];
  // Keep all three report images distinct, including across pool refills.
  const available = remaining.filter((footer) => !recent.includes(footer));
  const footer = available[Math.floor(Math.random() * available.length)];
  remaining.splice(remaining.indexOf(footer), 1);
  recent = [...recent.slice(-1), footer];
  return footer;
}
