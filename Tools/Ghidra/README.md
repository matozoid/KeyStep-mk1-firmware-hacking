# Ghidra headless scripts

Analysis of the official KeyStep Mk1 firmware. The firmware and the Ghidra DB stay local
in `vendor/` (git-ignored); never commit them.

```bash
# 1. fetch + unpack (user-supplied, local only)
curl -L -o vendor/keystep_1_0_28.led \
  'https://www.arturia.com/api/download/products/keystep/firmware/keystep_Firmware_Update__1_0_28.led'
python3 Tools/Ledpacker/led_to_bin.py vendor/keystep_1_0_28.led vendor/keystep_1_0_28.bin

# 2. import + auto-analyse
export GHIDRA_HOME=~/dev/ghidra_12.2_PUBLIC
"$GHIDRA_HOME/support/analyzeHeadless" vendor/ghidra keystep \
  -import vendor/keystep_1_0_28.bin \
  -processor ARM:LE:32:Cortex -loader BinaryLoader -loader-baseAddr 0x08004000 \
  -scriptPath Tools/Ghidra -preScript MapMemory.java \
  -postScript ApplySymbols.java Docs/re/symbols.tsv -overwrite

# 3. query
"$GHIDRA_HOME/support/analyzeHeadless" vendor/ghidra keystep -process keystep_1_0_28.bin \
  -noanalysis -scriptPath Tools/Ghidra -postScript DecompileFunc.java 0x08004148
```
