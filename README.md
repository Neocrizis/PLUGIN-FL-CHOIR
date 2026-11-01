# Chorale

Instrument de chœur pour FL Studio sur Mac (VST3, AU et application autonome). Il chante les paroles que tu tapes, une syllabe par note, avec quatre pupitres : soprano, alto, ténor et basse.

Le son est entièrement synthétisé par formants : il n'utilise ni échantillons ni IA, et rien ne quitte l'ordinateur.

## Compiler sur Mac

Il faut Xcode et CMake. Le premier build télécharge JUCE, donc une connexion internet est nécessaire.

```bash
xcode-select --install
```

```bash
brew install cmake
```

Depuis ce dossier :

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --target Chorale_VST3 Chorale_AU Chorale_Standalone
```

Le build copie les plugins dans `~/Library/Audio/Plug-Ins/VST3` et `~/Library/Audio/Plug-Ins/Components`. L'application autonome se trouve dans `build/Chorale_artefacts/Release/Standalone`.

## Tester le moteur et écouter un exemple

```bash
clang++ -std=c++17 -O2 tests/render_test.cpp -o render_test
```

```bash
./render_test .
```

La commande affiche les résultats des tests et écrit `gloria.wav`.

## Ouvrir dans FL Studio

1. Ouvre `Options > Manage plugins`, puis lance `Find installed plugins`.
2. Ajoute **Chorale** au Channel Rack comme générateur.
3. Tape les paroles, puis joue la mélodie au piano roll.

## Écrire les paroles

- **Chaque note chante la syllabe suivante.** Les notes jouées ensemble, à moins de 30 ms d'écart, partagent la même syllabe.
- **Le texte boucle** à la fin, et repart du début quand la lecture démarre ou revient en arrière.
- **Écris comme ça se prononce**, avec des tirets si le découpage automatique ne convient pas : `A-men`, `Glo-ri-a`, `la vie est bel-le`.
- **Toutes les lettres sont prononcées** : écris `bel-le` pour faire chanter le e, et `bel` sinon.
- **Les nasales ne sont pas modélisées** : `chanson` se chante « chane-sone ».

## Licence de JUCE

Le plugin s'appuie sur JUCE. Avant de le vendre ou de le distribuer, vérifie la licence JUCE qui s'applique à ta situation.
