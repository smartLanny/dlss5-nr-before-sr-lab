# Extracting this product branch to a standalone repository

The current GitHub connection could create an isolated product branch but did
not expose repository/fork creation. The branch root is therefore deliberately
self-contained and can be moved without carrying the experimental NR source.

From a local clone:

```powershell
git fetch origin product/xiaofeng-manager-foundation
git switch product/xiaofeng-manager-foundation

# Create an independent history containing the current product tree.
git switch --orphan standalone-main
git add -A
git commit -m "Initial 装机宅版 DLSS 5 manager foundation"

git remote add manager <NEW-REPOSITORY-URL>
git push -u manager standalone-main:main
```

After moving it, keep `UPSTREAM.md`, `LICENSE` and `THIRD_PARTY_NOTICES.md`.
Those files preserve the required DLSS5-Swapper attribution and the selected
upstream baseline.
