# Repository audit procedure

Run these checks from the repository root before every push.

## Inventory

```powershell
git status --short
git ls-files
```

Expected tracked categories are source, configuration, documentation, the build
script, and the minimal MinHook source subset. There should be no generated output.

## Binary and payload scan

```powershell
git ls-files | Select-String -Pattern '\.(dll|exe|addon64|addon32|pdb|lib|obj|o|zip|7z|bin|onnx|pt|pth|weights|dmp|etl)$'
```

The command must return no matches.

## Path, credential, and raw-log scan

```powershell
rg -n -i 'C:\\Users\\|Program Files|Steam\\steamapps|AppData|D:\\[0-9A-Za-z _.-]+\\|ghp_[A-Za-z0-9]+|github_pat_|AKIA[0-9A-Z]{16}|BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY|Authorization: Bearer|password\s*=' .
rg --files . | rg -i '\.(log|dmp|etl|zip|7z|rar)$'
```

Review every match manually. Documentation examples must remain generic and no
match may contain a real username, local game path, credential, or raw tester log.

## Source provenance

- Compare `src/` hashes to the intended reviewed source revision.
- Preserve `THIRD_PARTY_NOTICES.txt` and `third_party/minhook/LICENSE.txt`.
- Confirm no NVIDIA SDK header/source, proprietary DLL, model, or extracted
  binary data was introduced.
