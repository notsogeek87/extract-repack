# ArcExtract

Application Android native (Kotlin/Compose + C++/NDK) pour extraire, sur
l'appareil, des installateurs **Inno Setup** contenant des archives
**FreeArc/ARC** — le cas typique des repacks de jeux (FitGirl et
similaires) où `setup.exe` s'accompagne de fichiers `.bin` compressés avec
FreeArc.

Package : `eu.lielu.arcextract`. Aucun serveur, aucune connexion Internet,
aucune dépendance à Wine/Winlator/un PC pour extraire.

## Avant de coder : lisez `docs/ANALYSIS.md`

Ce dépôt a été construit en respectant l'ordre demandé : analyse des
projets open source existants → compréhension exacte du format ARC →
POC de lecture du header → POC de listing → extraction d'un petit fichier →
JNI → UI. **[docs/ANALYSIS.md](docs/ANALYSIS.md)** documente chacune de ces
étapes avec ce qui a été réellement vérifié dans cette session (pas
seulement écrit) : format binaire du conteneur ARC retrouvé dans le vrai
code source de FreeArc, matrice de capacités honnête (ce qui extrait
vraiment vs. ce qui est architecturé mais pas encore câblé), et pourquoi
certaines méthodes (SREP, XTool) ne sont pas supportées.

**[docs/BUILDING.md](docs/BUILDING.md)** donne les instructions de
compilation et, surtout, la liste précise et concrète de ce qu'il reste à
faire pour aller plus loin.

## État actuel (résumé honnête)

| Fonctionnalité | État |
|---|---|
| Détection Inno Setup (par contenu) | ✅ Implémenté et testé |
| Détection ARC/FreeArc (`ArC` magic) | ✅ Implémenté et testé |
| Listing complet d'une archive ARC (sans tout extraire) | ✅ Implémenté et testé, y compris contre les octets réels d'un `.bin` FitGirl |
| Extraction de fichiers "stockés" (non compressés) | ✅ Implémenté et testé bout-en-bout |
| Extraction LZMA/LZMA2/PPMd/REP (FreeArc) | 🧩 Backend et sous-module vendorisés, décodage non câblé |
| Extraction Inno Setup (via `innoextract`) | 🧩 Sous-module vendorisé, décodage non câblé |
| SREP, XTool | ❌ Hors périmètre, détectés et signalés proprement |
| Sécurité (path traversal, symlinks, noms invalides...) | ✅ Implémenté et testé |
| UI Compose (sélection, arbre, progression, résultat) | ✅ Écrite selon la spec, non compilée dans ce bac à sable (pas de SDK Android) |
| Tests unitaires (`:core`) | ✅ 49 tests, exécutés et passants |
| Tests natifs (`native/`) | ✅ 27 assertions, exécutées via CTest et passantes |

Voir `docs/ANALYSIS.md` §5 pour le détail complet et §9 pour pourquoi
certaines parties ne sont pas allées plus loin dans cette session (pas de
SDK/NDK Android disponible dans l'environnement de développement).

## Architecture

```
UI (Kotlin/Compose, module :app)
    ↓
Extracteur Kotlin (module :core — pur JVM, testable sans Android)
    ↓  JNI
Moteur natif C++ (native/) — parseur ARC + pont JNI
    ↓
Backends de compression (native/third_party/ : innoextract, FreeArc)
```

Détail complet, y compris le format binaire ARC octet par octet et le
tableau des méthodes de compression : [docs/ANALYSIS.md](docs/ANALYSIS.md).

## Structure du dépôt

```
core/                      Module Kotlin/JVM pur : détection, arbre de
                            fichiers, sécurité, estimation d'espace disque.
                            49 tests unitaires (JUnit 5).
app/                        Application Android (Compose/Material 3).
native/
  arc/                      Parseur du conteneur ARC (C++17, sans dépendance).
  jni/                      Pont JNI entre Kotlin et native/arc/.
  tools/arc_probe.cpp       CLI de diagnostic (équivalent interne de
                            `innoextract --list` pour la couche ARC).
  tests/                    Suite de tests C++ + fixtures binaires générées.
  third_party/              Sous-modules Git : innoextract (MIT/zlib),
                            FreeArc (GPL-2.0) — voir licence ci-dessous.
docs/
  ANALYSIS.md               Analyse technique complète (à lire en premier).
  BUILDING.md                Instructions de build + prochaines étapes.
```

## Compiler

Résumé — voir [docs/BUILDING.md](docs/BUILDING.md) pour le détail :

```bash
git clone --recurse-submodules <ce-dépôt>
# Ouvrir dans Android Studio (Giraffe+ recommandé), laisser Gradle
# synchroniser, installer le NDK proposé par Android Studio.
```

Pour vérifier uniquement le moteur natif (sans Android Studio ni NDK) :

```bash
cd native
python3 tests/fixtures/generate_fixtures.py   # régénère les fixtures si besoin
cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure
```

## Licence

Ce projet combine du code sous plusieurs licences :
- Code propre à ArcExtract (`app/`, `core/`, `native/arc/`, `native/jni/`) : MIT.
- `native/third_party/innoextract` (sous-module) : MIT/zlib.
- `native/third_party/freearc` (sous-module) : GPL-2.0.

Toute portion qui lie ou intègre du code de `native/third_party/freearc`
place le binaire résultant sous GPL-2.0 — voir
[docs/ANALYSIS.md §3.4](docs/ANALYSIS.md) pour le détail.
