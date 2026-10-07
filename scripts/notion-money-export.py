#!/usr/bin/env python3
"""Export the Notion money databases as the JSON of POST /api/v1/money/import/notion.

The one-off move of the owner's money from Notion (spec §9 of the money
section). Two steps, both here:

1. read the five databases (Currencies, Accounts, Categories, Income &
   Expense, Transfers) into plain rows: {"id": <page id>, "props": {name: value}}
   where a title or text is a string, a select its option name, a multi-select
   a list of names, a relation a list of page ids and a date
   {"start": ..., "is_datetime": bool};
2. map the plain rows onto the import format, fixing what the Notion model
   allowed and the Life OS model does not, and say so in "notes".

Reading either goes to the Notion API (NOTION_TOKEN, data source ids below)
or takes a directory of plain rows already dumped (--dump DIR with
currencies.json, accounts.json, categories.json, transactions.json,
transfers.json). Nothing of it runs in the cluster.

Usage:
  NOTION_TOKEN=secret_... scripts/notion-money-export.py > money.json
  scripts/notion-money-export.py --dump ./notion-dump > money.json
  curl -X POST -H "Authorization: Bearer $KEY" -H 'Content-Type: application/json' \\
       --data @money.json https://life.example/api/v1/money/import/notion

The API path uses the data-source endpoints of Notion-Version 2025-09-03; it
was written against the documentation and not run against a live workspace
(the owner's move went through --dump).
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

# The owner's data sources (Finances hub, checked 2026-10-07).
SOURCES = {
    "currencies": "e1d7fe31-2f7f-41f0-884a-afd610ae08ed",
    "accounts": "b796a4a9-e4c5-8230-b8c2-875f5758f2a3",
    "categories": "8556a4a9-e4c5-8258-8e21-0731624b43ef",
    "transactions": "9ea6a4a9-e4c5-838b-a127-87624b5cef21",
    "transfers": "29cface7-cd8d-4ac5-a93b-b84eb37a2eaa",
}

NOTION_VERSION = "2025-09-03"

ACCOUNT_KINDS = {
    "Cash": "cash",
    "Savings": "deposit",
    "Debit Card": "card",
    "Credit Card": "card",
    "Current": "other",
}
TYPES = {"Income": "income", "Expense": "expense", "FX Adjustment": "fx_adjustment"}
# Currencies whose amounts are not shown with two decimals.
DECIMALS = {"VND": 0, "JPY": 0, "BTC": 8}


# ── reading ─────────────────────────────────────────────────────────────────


def plain_value(prop: dict):
    """One property of the Notion API as a plain value."""
    kind = prop.get("type")
    value = prop.get(kind)
    if kind in ("title", "rich_text"):
        return "".join(part.get("plain_text", "") for part in value or [])
    if kind == "number":
        return value
    if kind == "select":
        return value["name"] if value else None
    if kind == "multi_select":
        return [v["name"] for v in value or []]
    if kind == "relation":
        return [v["id"].replace("-", "") for v in value or []]
    if kind == "date":
        if not value:
            return None
        # With a time_zone the API answers local time without an offset.
        return {"start": value["start"], "is_datetime": "T" in value["start"], "time_zone": value.get("time_zone")}
    return None  # formulas and rollups are not read: the import recomputes


def fetch(source_id: str, token: str) -> list[dict]:
    rows, cursor = [], None
    while True:
        body = {"page_size": 100}
        if cursor:
            body["start_cursor"] = cursor
        req = urllib.request.Request(
            f"https://api.notion.com/v1/data_sources/{source_id}/query",
            data=json.dumps(body).encode(),
            headers={
                "Authorization": f"Bearer {token}",
                "Notion-Version": NOTION_VERSION,
                "Content-Type": "application/json",
            },
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=60) as resp:
            page = json.load(resp)
        for r in page["results"]:
            rows.append(
                {
                    "id": r["id"].replace("-", ""),
                    "props": {k: plain_value(v) for k, v in r["properties"].items()},
                }
            )
        if not page.get("has_more"):
            return rows
        cursor = page["next_cursor"]


def read(args) -> dict[str, list[dict]]:
    if args.dump:
        return {name: json.loads((Path(args.dump) / f"{name}.json").read_text()) for name in SOURCES}
    token = os.environ.get("NOTION_TOKEN")
    if not token:
        sys.exit("NOTION_TOKEN is not set (or pass --dump DIR)")
    return {name: fetch(source, token) for name, source in SOURCES.items()}


# ── mapping ─────────────────────────────────────────────────────────────────


def first(ids) -> str | None:
    return ids[0] if ids else None


def local_date_time(date: dict | None, offset: dt.timedelta) -> tuple[str | None, str | None]:
    """A Notion date as the day and the HH:MM where the owner was; a date without a time stays a day."""
    if not date or not date.get("start"):
        return None, None
    start = date["start"]
    if not date.get("is_datetime"):
        return start[:10], None
    moment = dt.datetime.fromisoformat(start.replace("Z", "+00:00"))
    if moment.tzinfo is None:
        # Local time in the zone Notion names; without one it is already where the owner was.
        if not date.get("time_zone"):
            return moment.date().isoformat(), moment.strftime("%H:%M")
        from zoneinfo import ZoneInfo

        moment = moment.replace(tzinfo=ZoneInfo(date["time_zone"]))
    moment = moment.astimezone(dt.timezone(offset))
    return moment.date().isoformat(), moment.strftime("%H:%M")


# A currency code ("THB") or a word that names no shop.
_NOT_A_MERCHANT = re.compile(r"^(?:[A-Z]{3}|(?i:cash|store))$")


def merchant_of(name: str) -> str:
    """The merchant the owner wrote in the parentheses of a name: "Groceries (7-Eleven Choengthale Soi 14, THB)"."""
    m = re.search(r"\(([^()]*)\)\s*$", name)
    if not m:
        return ""
    for part in (p.strip() for p in m.group(1).split(",")):
        if part and not _NOT_A_MERCHANT.match(part):
            return part
    return ""


def last4_of(digits: str | None, name: str) -> str:
    if digits and re.fullmatch(r"\d{4}", digits):
        return digits
    m = re.search(r"•(\d{4})", name)
    return m.group(1) if m else ""


def convert(rows: dict[str, list[dict]], offset: dt.timedelta) -> dict:
    notes: list[str] = []
    code_of = {r["id"]: r["props"].get("Code") for r in rows["currencies"]}

    currencies = []
    for r in rows["currencies"]:
        p = r["props"]
        code = p.get("Code")
        role = (p.get("Role") or "").lower() or None
        currencies.append(
            {"code": code, "name": p.get("Name") or "", "role": role, "decimals": DECIMALS.get(code, 2)}
        )

    accounts = []
    for r in rows["accounts"]:
        p = r["props"]
        name = p.get("Name") or ""
        types = p.get("Account Type") or []
        kind = ACCOUNT_KINDS.get(types[0], "other") if types else "other"
        if p.get("Credit Limit"):
            notes.append(f"account {name}: credit limit {p['Credit Limit']} not carried over (no such field)")
        opened, _ = local_date_time(p.get("Opened"), offset)
        accounts.append(
            {
                "external_id": r["id"],
                "name": name,
                "bank": p.get("Bank") or "",
                "kind": kind,
                "currency": code_of.get(first(p.get("Currency"))),
                "last4": last4_of(p.get("Last 4 Digits"), name),
                "opening_balance": p.get("Starting Balance") or 0,
                "opening_date": opened,
            }
        )

    categories = []
    for r in rows["categories"]:
        p = r["props"]
        name = p.get("Category") or ""
        kind = (p.get("Kind") or "").lower()
        if not kind:
            kind = "expense"
            notes.append(f"category {name}: Kind empty, taken as expense")
        budget, currency = p.get("Max") or 0, code_of.get(first(p.get("Budget Currency")))
        if budget > 0 and not currency:
            notes.append(f"category {name}: budget {budget} has no currency, left without a budget")
        has_budget = budget > 0 and bool(currency)
        categories.append(
            {
                "external_id": r["id"],
                "name": name,
                "kind": kind,
                "flexibility": (p.get("Flexibility") or "variable").lower(),
                "budget_max": budget if has_budget else None,
                "budget_currency": currency if has_budget else None,
            }
        )

    transactions = []
    for r in rows["transactions"]:
        p = r["props"]
        name = p.get("Transaction") or ""
        kind = TYPES.get(p.get("Type"), "expense")
        if kind == "income":
            account = first(p.get("To Account")) or first(p.get("From Account"))
        else:
            account = first(p.get("From Account")) or first(p.get("To Account"))
        date, time = local_date_time(p.get("Date"), offset)
        row = {
            "external_id": r["id"],
            "type": kind,
            "date": date,
            "time": time,
            "account": account,
            "amount": p.get("Amount"),
            "category": None if kind == "fx_adjustment" else first(p.get("Category")),
            "merchant": "" if kind == "fx_adjustment" else merchant_of(name),
            "name": name,
            "receipt_amount": p.get("Receipt Amount"),
            "receipt_currency": code_of.get(first(p.get("Receipt Currency"))),
            "fx_note": p.get("FX Note") or "",
            "adjusts": first(p.get("Adjusts")) if kind == "fx_adjustment" else None,
        }
        if row["receipt_amount"] is None or row["receipt_currency"] is None:
            row["receipt_amount"] = row["receipt_currency"] = None
        transactions.append(row)

    currency_of = {a["external_id"]: a["currency"] for a in accounts}
    transfers = []
    for r in rows["transfers"]:
        p = r["props"]
        date, _ = local_date_time(p.get("Date"), offset)
        source, target = first(p.get("From Account")), first(p.get("To Account"))
        received = p.get("Amount Received")
        if received is not None and currency_of.get(source) == currency_of.get(target):
            # One currency on both sides: the model keeps the received amount empty.
            if received != p.get("Amount Sent"):
                notes.append(f"transfer {p.get('Transfer')}: received {received} dropped, both sides in one currency")
            received = None
        transfers.append(
            {
                "external_id": r["id"],
                "date": date,
                "from_account": source,
                "to_account": target,
                "amount_sent": p.get("Amount Sent"),
                "amount_received": received,
                "name": p.get("Transfer") or "",
                "note": p.get("Notes") or "",
            }
        )

    if any(t["category"] is None and t["type"] == "fx_adjustment" for t in transactions):
        notes.append("fx adjustments: the category stays with the original row")
    notes.append(f"times converted from UTC to UTC{offset_text(offset)}")

    return {
        "currencies": currencies,
        "accounts": accounts,
        "categories": categories,
        "transactions": transactions,
        "transfers": transfers,
        "notes": notes,
    }


def offset_text(offset: dt.timedelta) -> str:
    minutes = int(offset.total_seconds() // 60)
    sign = "+" if minutes >= 0 else "-"
    return f"{sign}{abs(minutes) // 60:02d}:{abs(minutes) % 60:02d}"


def parse_offset(text: str) -> dt.timedelta:
    m = re.fullmatch(r"([+-])(\d{2}):(\d{2})", text)
    if not m:
        raise argparse.ArgumentTypeError("expected +HH:MM")
    delta = dt.timedelta(hours=int(m.group(2)), minutes=int(m.group(3)))
    return delta if m.group(1) == "+" else -delta


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dump", help="directory of plain rows instead of the Notion API")
    ap.add_argument(
        "--utc-offset",
        type=parse_offset,
        default=parse_offset("+07:00"),
        help="where the owner was when the rows with a time were written (default +07:00)",
    )
    args = ap.parse_args()
    json.dump(convert(read(args), args.utc_offset), sys.stdout, ensure_ascii=False, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
