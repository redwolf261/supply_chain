"""Checks that the documentation only refers to things that exist.

For every Markdown file in the project root and docs/, finds paths written in backticks (for example
`backend/src/api/ApiService.cpp` or `docs/ALGORITHMS.md`) and reports any that do not exist. Also checks that every
document listed as required by the specification is present. Exit code 1 if anything is missing.

Usage:  python scripts/check_docs.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = ["README.md", "PROJECT_PROGRESS.md"] + [f"docs/{n}.md" for n in [
    "PROJECT_OVERVIEW", "PROBLEM_STATEMENT", "MATHEMATICAL_MODEL", "SYSTEM_ARCHITECTURE", "ALGORITHMS", "DAA_SYLLABUS_MAPPING",
    "COMPLEXITY_ANALYSIS", "CORRECTNESS_AND_TESTING", "EXPERIMENTAL_EVALUATION", "INSTALLATION_GUIDE", "USER_MANUAL",
    "VIVA_PREPARATION", "LIMITATIONS_AND_FUTURE_WORK", "CPP_CONCEPTS_EXPLAINED"]]

# A backticked token that looks like a repository path: has a slash or a known extension, no spaces or wildcards.
PATH = re.compile(r"`([A-Za-z0-9_.\-\\/]+\.(?:md|cpp|h|ts|tsx|mjs|py|ps1|json|csv|txt|png)|[A-Za-z0-9_.\-]+/[A-Za-z0-9_.\-\\/]*)`")
SEARCH_DIRS = ["", "docs", "backend", "backend/include", "backend/src", "backend/tests", "frontend", "frontend/src",
               "experiments", "experiments/results", "scripts", "data"]


def exists(token: str) -> bool:
    token = token.replace("\\", "/").rstrip("/")
    if any(ch in token for ch in "*{}|<>") or token.startswith(("http", "/api")):
        return True                                    # patterns, URLs and API routes are not files
    if token.startswith("../"):
        return True                                    # quoted test inputs (path-traversal attempts), not references
    for base in SEARCH_DIRS:
        if (ROOT / base / token).exists():
            return True
        # the docs name a C++ module as `graph/SupplyChainGraph`, meaning its .h and .cpp pair
        if any((ROOT / base / (token + ext)).exists() for ext in (".h", ".cpp")):
            return True
    # bare file names such as `ALGORITHMS.md` or `ApiService.cpp`: look anywhere except dependencies and build output
    if "/" not in token:
        skip = ("node_modules", "build", "third_party", "dist", ".git")
        return any(not any(s in p.parts for s in skip) for p in ROOT.rglob(token))
    return False


def main() -> int:
    problems = []
    for required in REQUIRED:
        if not (ROOT / required).exists():
            problems.append(f"missing required document: {required}")
    docs = [ROOT / "README.md", ROOT / "PROJECT_PROGRESS.md"] + sorted((ROOT / "docs").glob("*.md"))
    checked = 0
    for doc in docs:
        text = doc.read_text(encoding="utf-8")
        if any(ord(c) < 32 and c not in "\n\r\t" for c in text):
            problems.append(f"{doc.relative_to(ROOT)}: contains control characters")
        for token in sorted(set(PATH.findall(text))):
            checked += 1
            if not exists(token):
                problems.append(f"{doc.relative_to(ROOT)}: refers to `{token}`, which does not exist")
    print(f"{len(docs)} documents, {checked} path references checked")
    for p in problems:
        print("  " + p)
    print("OK" if not problems else f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
