from PyInstaller.utils.hooks import collect_data_files, collect_dynamic_libs

rdkit_data = collect_data_files("rdkit")
rdkit_bins = collect_dynamic_libs("rdkit")

a = Analysis(
    ["../chemistry/helper.py"],
    pathex=[".."],
    binaries=rdkit_bins,
    datas=rdkit_data,
    hiddenimports=["discovery", "resolution", "rdkit.Chem.rdMolDescriptors"],
    # numpy and PIL are reachable only through rdkit.Chem.Crippen/Descriptors/Lipinski,
    # which chemistry/helper.py no longer imports. Excluding them removes 18 of the 95
    # Mach-O files one analysis maps, each of which macOS evaluates individually on first
    # execution (#97, docs/chemistry-cold-start.md). Analysis output is unchanged.
    excludes=["tkinter", "pytest", "numpy", "PIL"],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="iupac-analysis-helper", console=True)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name="helper")
