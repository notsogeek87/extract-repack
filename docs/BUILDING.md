# Compilation et prochaines étapes

## 1. Prérequis

- Android Studio (Koala/2024.1 ou plus récent recommandé).
- JDK 17 (celui fourni par Android Studio convient).
- NDK — Android Studio proposera de l'installer automatiquement à
  l'ouverture du projet (`native/CMakeLists.txt` est référencé depuis
  `app/build.gradle.kts`, `externalNativeBuild.cmake.path`).
- CMake ≥ 3.22 (fourni par le SDK Manager d'Android Studio).

## 2. Cloner avec les sous-modules

```bash
git clone --recurse-submodules <url-du-dépôt>
# ou, si déjà cloné sans --recurse-submodules :
git submodule update --init --recursive
```

Deux sous-modules sont vendorisés dans `native/third_party/` :
- `innoextract` (dscharrer/innoextract, MIT/zlib) — couche Inno Setup.
- `freearc` (j2969719/freearc-old, GPL-2.0) — sources C++ originales de
  FreeArc 0.67, y compris `Compression/LZMA2`, `Compression/PPMD`,
  `Compression/REP`, `Unarc/`.

Ni l'un ni l'autre n'est actuellement compilé dans `native/jni/CMakeLists.txt`
— voir §4.

## 3. Ouvrir et compiler

Ouvrir le dossier racine dans Android Studio, laisser Gradle synchroniser,
puis *Run*. `app/build.gradle.kts` cible `arm64-v8a` uniquement
(`ndk.abiFilters`), conformément à l'appareil visé (Galaxy Z Fold, ARM64) —
ajoutez `"x86_64"` si vous voulez aussi tester sur l'émulateur.

### Compiler/tester uniquement le moteur natif (sans Android Studio)

Utile pour itérer sur `native/arc/` sans passer par tout le cycle Gradle :

```bash
cd native
cmake -S . -B build
cmake --build build -j
cd build && ctest --output-on-failure
./arc_probe list ../tests/fixtures/sample_store.arc
```

Ceci utilise le compilateur hôte (g++/clang), pas le NDK — c'est exactement
ainsi que ce module a été développé et vérifié dans cette session (voir
`docs/ANALYSIS.md` §7). Régénérez les fixtures si vous modifiez le format :
`python3 tests/fixtures/generate_fixtures.py`.

## 4. Prochaines étapes concrètes (ce qui reste à câbler)

Tout ce qui suit est **architecturé mais pas implémenté** — voir la matrice
de capacités dans `docs/ANALYSIS.md` §5 pour le détail de ce qui marche
déjà réellement.

### 4.1. Décompression FreeArc réelle (LZMA2 / PPMd / REP)

Fichiers à toucher :
- `native/jni/CMakeLists.txt` : ajouter un `add_subdirectory` (ou des
  `add_library` directs) pour `native/third_party/freearc/Compression/LZMA2`,
  `.../PPMD`, `.../REP`, en **excluant explicitement** les fichiers `.asm`
  du dossier `Compression/` (`7zAsm.asm`, `7zCrcT8U.asm`, `AesOpt.asm` —
  spécifiques x86, voir `docs/ANALYSIS.md` §3.3 ; le décodage lui-même est
  en C/C++ portable, ce n'est que l'accélération CRC/AES en asm qui ne
  compile pas pour ARM64).
- Écrire un adaptateur C++ (`native/arc/codec_dispatch.{h,cpp}` par
  exemple) qui appelle les points d'entrée de décompression de
  `CompressionLibrary.cpp`/`CELS.cpp` (interface `DecompressMem`/`Decompress`
  du fichier `Compression.h` vendorisé) pour les chaînes de méthode
  `"lzma2:..."`, `"ppmd:..."`, `"rep:..."`.
- Dans `native/arc/arc_reader.cpp`, la fonction `decompressBlock()` est le
  point d'extension : aujourd'hui elle ne gère que `compressor == "store"`
  et lève `UnsupportedCompressorError` pour tout le reste — brancher
  l'adaptateur ci-dessus à la place de ce `throw`.
- **Point d'architecture important à ne pas oublier côté UI** : FreeArc
  compresse les fichiers par groupes ("solid blocks"). Décoder LZMA2/PPMd
  est un processus **séquentiel** — extraire un seul fichier situé en fin
  d'un solid block nécessite de décompresser tout ce qui le précède dans
  ce même bloc. `ArcEntry.dataBlockIndex` (voir `native/arc/arc_types.h`)
  identifie déjà le solid block de chaque fichier ; il manque encore le
  message d'avertissement utilisateur prévu par la spec ("Si le moteur
  doit parcourir une archive entière pour atteindre un fichier,
  l'expliquer clairement à l'utilisateur") — à ajouter dans
  `core/model/UserMessage.kt` et à déclencher côté `ExtractionEngine`
  quand une sélection partielle tombe au milieu d'un gros solid block.
- Une fois câblé, changer le `SupportStatus` de `LZMA`, `LZMA2`, `PPMD`,
  `REP` dans `core/model/CompressionMethod.kt` de `PLANNED` à
  `EXTRACTABLE` — **seulement après un test réel** qui décompresse
  effectivement un fichier et vérifie son CRC (voir la philosophie de
  `native/tests/arc_reader_test.cpp`, à étendre avec un fixture
  LZMA2 réel).

### 4.2. Décodage Inno Setup réel (via `innoextract`)

Fichiers à toucher :
- `native/jni/CMakeLists.txt` : ajouter `add_subdirectory(third_party/innoextract)`
  et lier `arcextract_jni` à sa bibliothèque. `innoextract` dépend de
  **Boost** (`find_package(Boost REQUIRED COMPONENTS ...)`, voir son
  `CMakeLists.txt`) et de **liblzma** — c'est le principal travail restant
  ici : croiser Boost pour Android ARM64. `alanwoolley/innoextract-android`
  (MIT, cité dans `docs/ANALYSIS.md` §2) est un portage Android existant
  d'innoextract et sert de référence directe pour cette étape (comment il
  a résolu la dépendance Boost, quelle version de Boost, etc.) — ne pas
  repartir de zéro sans l'avoir étudié.
- Écrire `InnoSetupBackend.list()`/`.extract()` (actuellement dans
  `app/src/main/java/eu/lielu/arcextract/backend/InnoSetupBackend.kt`,
  qui lève explicitement une exception "non câblé") en s'appuyant sur un
  nouveau pont JNI (`native/jni/inno_setup_jni.cpp`) qui appelle l'API de
  listing/extraction d'innoextract (`loader::processor`, voir son `src/`).

### 4.3. SREP, XTool

Volontairement non planifiés dans le backlog immédiat — voir
`docs/ANALYSIS.md` §3.2 pour l'analyse de faisabilité (dictionnaire SREP
trop gros pour un budget mémoire mobile fiable ; XTool est une chaîne de
pré-traitement avec de nombreuses dépendances tierces, projet à part
entière). Si support néanmoins souhaité un jour : commencer par
`Bulat-Ziganshin/MT-LZ` ou `YadeWira/omega-srep` (continuation active de
SREP) pour évaluer un portage ciblé "faible dictionnaire" plutôt que le
mode plein volume desktop.

### 4.4. Tests instrumentés Android

Aucun test `androidTest` n'existe encore (SAF/Compose nécessitent un
appareil ou un émulateur pour être testés réellement — indisponibles dans
l'environnement où ce projet a été développé). À ajouter :
- Test SAF de bout en bout avec un petit `.arc` de test poussé sur
  l'appareil via `adb push`, ouvert via un `DocumentsProvider` de test.
- Test Compose (`createAndroidComposeRule`) pour l'écran d'arbre : vérifier
  qu'une sélection de dossier sélectionne bien tous ses descendants et
  aucun frère (déjà couvert côté logique pure par
  `core/src/test/.../FileTreeTest.kt`, mais pas encore côté rendu réel).

## 5. Limitations connues de cette version

- `native/jni/` compile proprement contre un vrai `jni.h` (vérifié dans
  cette session, voir `docs/ANALYSIS.md` §7/§9) mais n'a **jamais été
  compilé avec le NDK ni exécuté sur un appareil ou un émulateur** — le
  bac à sable où ce dépôt a été créé n'a ni SDK ni NDK Android installés.
  Premier `./gradlew assembleDebug` : à faire, à corriger si nécessaire.
- L'UI Compose n'a pas pu être compilée (même limitation : pas de SDK
  Android ici pour résoudre `com.android.application` depuis
  `dl.google.com`, injoignable depuis ce bac à sable).
- La détection Inno Setup (`InnoSetupDetector`) est une heuristique de
  contenu (recherche de la chaîne `"Inno Setup Setup Data ("`) — robuste
  en pratique mais pas une garantie à 100 % ; un futur portage réel
  d'innoextract (§4.2) la rendra inutile pour la décision finale
  d'extraction (seulement pour l'affichage rapide "Installateur détecté").
