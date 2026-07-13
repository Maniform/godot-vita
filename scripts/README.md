# Scripts de construction

## Préparation complète

`setup_godot_vita.sh` prépare un Ubuntu 24.04, installe VitaSDK et PVR_PSP2,
configure MinGW, puis construit les éditeurs et modèles d’exportation :

```bash
scripts/setup_godot_vita.sh
```

Le script ajoute un bloc géré à `~/.bashrc` pour `VITASDK` et `PATH`. Il est
réexécutable; les compilations SCons utilisent le cache du dépôt. Si VitaSDK
est déjà installé, le bootstrap et `install-all.sh` sont ignorés et seul
`vitasdk-update` est exécuté. `FORCE_VITASDK_INSTALL=yes` permet de forcer une
réinstallation complète.

Options principales :

- `--install-only` : installation sans compilation ;
- `--build-only` : compilation sans installation ;
- `--with-cross-arch` : ajoute les binaires Windows pour l’architecture CPU
  opposée grâce à LLVM-MinGW.

La compilation Linux vers l’architecture CPU opposée n’est pas activée : le
port X11 de cette branche ne la prend pas encore en charge. Le binaire Linux
produit correspond donc toujours à l’architecture de l’hôte.

## Scripts spécialisés

- `install_vita_pvr_sdk.sh` installe PVR_PSP2 ;
- `install_windows_cross_dependencies.sh` installe LLVM-MinGW selon
  l’architecture de l’hôte ;
- `build_editors.sh` construit les éditeurs ;
- `build_export_templates.sh` construit et regroupe les modèles dans un TPZ.

VitaGL est désactivé par défaut. Pour le réactiver ponctuellement :

```bash
VITAGL=yes scripts/build_export_templates.sh vita
```
