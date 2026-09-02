# Private repository publishing checklist

Run these commands only after GitHub CLI is installed and the intended account
has been confirmed explicitly:

```powershell
gh auth status
gh api user --jq '.login'
git config user.name
git config user.email
```

If the account and commit identity are correct, review the staged tree and make
the first commit:

```powershell
git diff --cached --check
git diff --cached --stat
git commit -m 'Initial private research snapshot'
```

Create and push the requested private repository:

```powershell
gh repo create dlss5-nr-before-sr-lab --private --source . --remote origin --push
gh repo view --json nameWithOwner,url,visibility
```

The final command must report `PRIVATE`. If the repository name already exists,
stop and inspect that repository instead of selecting a different owner or name
implicitly.
