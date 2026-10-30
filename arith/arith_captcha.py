"""Solve Boltbook's obfuscated arithmetic verification challenge.

The challenge is a two-number, one-operation word problem wrapped in noise
(`A- LIGHTning boLT ...`). Two failure modes cost a one-shot verification code,
so both are handled explicitly:

* Unit phrases such as "meters per second" are not division. "per <unit>" is
  stripped before parsing, otherwise "45 m/s that slows by 7" reads as 45/7.
* "and" is a conjunction, not always addition ("and slows by seven"). Operators
  are therefore attached to the number gap they follow, and a specific operator
  (minus/times/divide) wins over the generic "and"/"plus" in the same gap.
"""
import json
import re
import sys
import time

sys.path.insert(0, "/root/scripts/boltbook")
import collab  # noqa: E402

UNITS = {
    "zero": 0, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16, "seventeen": 17,
    "eighteen": 18, "nineteen": 19, "twenty": 20, "thirty": 30, "forty": 40,
    "fifty": 50, "sixty": 60, "seventy": 70, "eighty": 80, "ninety": 90,
}
MULT = {"hundred": 100, "thousand": 1000, "million": 1000000}

DIV = ("divide", "divided", "split", "share", "shared", "sharing", "per",
       "quotient", "distributed", "evenly", "among", "between")
MUL = ("times", "multiplied", "multiply", "product", "double", "doubled", "triple",
       "tripled", "twice", "thrice", "each")
SUB = ("minus", "subtract", "subtracted", "difference", "less", "fewer", "loses",
       "lost", "removed", "spent", "spends", "spend", "spending", "eats", "eaten",
       "dropped", "drops", "drop", "slows", "slow", "slowed", "decreases",
       "decrease", "decreased", "reduces", "reduced", "falls", "fell", "declines",
       "declined", "cuts", "cut", "short", "buys", "bought", "pays", "paid",
       "gives", "gave", "given", "sends", "sent", "uses", "used", "consumes",
       "burns", "burned", "burnt", "destroys", "destroyed", "withdraws",
       "withdrew", "pays out", "sells", "sold", "costs", "cost")
ADD = ("plus", "add", "added", "sum", "total", "combined", "together", "gains",
       "gained", "grows", "more", "and", "increases", "increased", "rises",
       "rose", "receives", "received", "earns", "earned", "gets", "got",
       "deposits", "deposited", "wins", "won", "mints")

# "per second" is a rate unit, not an operation. Only physical units are listed:
# a countable noun after "per" ("split per person") is a real division.
RATE_UNITS = ("second", "seconds", "minute", "minutes", "hour", "hours", "day",
              "days", "week", "weeks", "month", "months", "year", "years",
              "meter", "meters", "metre", "metres", "kilometer", "kilometers",
              "mile", "miles", "liter", "liters", "litre", "litres", "gallon",
              "gallons", "kg", "kilogram", "kilograms", "pound", "pounds",
              "gram", "grams", "mph", "kph", "rpm")

# A specific operator beats a conjunction sitting in the same gap.
RANK = {"*": 3, "/": 3, "-": 2, "+": 1}

# A post-number modifier settles the gap outright: "buys nineteen MORE" is an
# addition even though "buys" alone would read as a subtraction. Same for
# "fewer"/"less", which always subtract.
FORCE_ADD = ("more", "extra", "additional", "again")
FORCE_SUB = ("fewer", "less")


def clean(text):
    """Drop obfuscation characters but keep letters, digits and spaces."""
    t = re.sub(r"[^A-Za-z0-9 ]", "", text)
    return re.sub(r"\s+", " ", t).strip().lower()


def strip_rates(text):
    """Remove 'per <unit>' so a rate does not parse as a division."""
    pattern = r"\bper (" + "|".join(RATE_UNITS) + r")\b"
    return re.sub(pattern, " ", text)


def parse(tokens):
    """Return (numbers, ops) with ops aligned to the gap after each number."""
    nums = []
    gaps = {}          # gap index -> operator
    i = 0
    while i < len(tokens):
        t = tokens[i]
        if t.isdigit():
            nums.append(float(t))
            i += 1
            continue
        if t in UNITS or t in MULT:
            total, cur, used = 0, 0, False
            while i < len(tokens) and (tokens[i] in UNITS or tokens[i] in MULT):
                w = tokens[i]
                if w in UNITS:
                    cur += UNITS[w]
                else:
                    m = MULT[w]
                    if m >= 1000:
                        total += (cur or 1) * m
                        cur = 0
                    else:
                        cur = (cur or 1) * m
                used = True
                i += 1
            if used:
                nums.append(float(total + cur))
            continue
        op = None
        if t in FORCE_ADD:
            op = "+!"
        elif t in FORCE_SUB:
            op = "-!"
        elif t in DIV:
            op = "/"
        elif t in MUL:
            op = "*"
        elif t in SUB:
            op = "-"
        elif t in ADD:
            op = "+"
        if op and nums:
            # A post-number modifier trails its own number, so it settles the
            # gap *before* it, not the empty one after the last number.
            gap = max(0, len(nums) - 2) if op.endswith("!") else len(nums) - 1
            old = gaps.get(gap)
            rank = {"+!": 9, "-!": 9}.get(op, RANK.get(op, 0))
            if old is None or rank > {"+!": 9, "-!": 9}.get(old, RANK.get(old, 0)):
                gaps[gap] = op
        i += 1
    # Collapse the forced markers back to their plain operator.
    plain = {"+!": "+", "-!": "-"}
    ops = [plain.get(gaps.get(g), gaps.get(g)) for g in range(max(0, len(nums) - 1))]
    return nums, ops


def evaluate(nums, ops):
    if not nums:
        raise ValueError("no numbers")
    if len(nums) == 1:
        return nums[0]
    ops = [(o or "+") for o in ops[: len(nums) - 1]]
    while len(ops) < len(nums) - 1:
        ops.append("+")
    # multiplication / division first
    vals, o = [nums[0]], []
    for i, op in enumerate(ops):
        nxt = nums[i + 1]
        if op == "*":
            vals[-1] *= nxt
        elif op == "/":
            vals[-1] /= nxt
        else:
            vals.append(nxt)
            o.append(op)
    acc = vals[0]
    for i, op in enumerate(o):
        acc = acc + vals[i + 1] if op == "+" else acc - vals[i + 1]
    return acc


def solve(text):
    tokens = strip_rates(clean(text)).split()
    nums, ops = parse(tokens)
    return evaluate(nums, ops), nums, ops


def solve_logged(text, tag=""):
    """solve() plus an append-only log of every challenge we were shown.

    A wrong answer burns one of three server-side attempts and the code is
    single-use, so the raw text must be on disk before we spend an attempt.
    """
    ans, nums, ops = solve(text)
    line = json.dumps({"at": int(time.time()), "tag": tag, "text": text,
                       "nums": nums, "ops": ops, "answer": f"{ans:.2f}"})
    with open("/root/scripts/boltbook/challenges.jsonl", "a") as f:
        f.write(line + "\n")
    return ans, nums, ops


def verify(challenge):
    ans, nums, ops = solve(challenge["challenge_text"])
    body = {"verification_code": challenge["verification_code"], "answer": f"{ans:.2f}"}
    r = collab.forum("/verify", body)
    print(f"  {nums} {ops} -> {ans:.2f} : {json.dumps(r)[:200]}")
    return r


if __name__ == "__main__":
    print(solve(sys.argv[1]))
