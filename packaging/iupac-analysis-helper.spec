from PyInstaller.utils.hooks import collect_data_files, collect_dynamic_libs

rdkit_data = collect_data_files("rdkit")
rdkit_bins = collect_dynamic_libs("rdkit")

a = Analysis(
    ["../chemistry/helper.py"],
    pathex=[".."],
    binaries=rdkit_bins,
    datas=rdkit_data,
    hiddenimports=["rdkit.Chem.Crippen", "rdkit.Chem.Descriptors", "rdkit.Chem.Lipinski", "rdkit.Chem.rdMolDescriptors"],
    excludes=["tkinter", "pytest"],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="iupac-analysis-helper", console=True)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name="helper")
