# Chorale

Effet de chœur en direct pour FL Studio sur Mac (VST3, AU et application autonome). Le chanteur chante dans son micro, et Chorale reprend ses mots et sa mélodie avec jusqu'à 32 voix réparties en quatre pupitres : soprano, alto, ténor et basse.

Il n'y a rien à taper et aucune IA : le chœur est fabriqué en temps réel à partir de la voix reçue, et rien ne quitte l'ordinateur. Le chœur arrive environ 20 ms après la voix, et jusqu'à 30 ms de plus selon le réglage Ensemble.

## Compilation

GitHub Actions compile le plugin à chaque envoi, sur un Mac dans le cloud. Le paquet se télécharge ici : [Ensemble.zip](https://github.com/Neocrizis/PLUGIN-FL-CHOIR/releases/download/latest/Ensemble.zip). `Exemple-audio`, un rendu produit à partir d'une voix de synthèse, se trouve sur la page de la compilation.

Pour compiler sur un Mac avec Xcode et CMake :

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --target Chorale_VST3 Chorale_AU Chorale_Standalone
```

## Tester le moteur

```bash
clang++ -std=c++17 -O2 tests/render_test.cpp -o render_test
```

```bash
./render_test .
```

## Licence de JUCE

Le plugin s'appuie sur JUCE. Avant de le vendre ou de le distribuer, vérifie la licence JUCE qui s'applique à ta situation.
