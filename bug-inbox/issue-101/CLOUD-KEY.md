# KEY extract #101 (异环 / 5080 ultrawide / beta0.4.4-test1)

## capture-info
```
build=beta0.4.4-test1
exe.leaf=HTGame.exe
gpu=NVIDIA GeForce RTX 5080
start.commandState.hooksReady=1
start.commandState.nullBaseline=1
start.commandState.hasPso=0
start.nrSuccess=5600
start.transferSubmitted=5580
start.bypass=command-state-unverified
end.nrSuccess=5600
end.transferSubmitted=5580
end.bypass=command-state-unverified
color.status=bypassed
color.reason=unverified-hdr-luminance-only
```

## Cloud read note
hooksReady=1 and high nrSuccess/transferSubmitted counts, but bypass still `command-state-unverified` and color bypassed. Distinguish prep complete / NR exec / composite submit stages. Early installing hooks still not terminal.
