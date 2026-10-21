# Gestion des stocks et logistique

Application de bureau native C++ en français pour les stocks, les entrepôts et le réapprovisionnement. Elle fonctionne hors ligne, utilise une base SQLite locale et ne lance aucun serveur web.

## Organisation du dossier

- `standalone/` : paquet Linux prêt à exécuter (`GestionStock-Linux/`) et son archive de distribution.
- `src/` : code principal à modifier (interface, base de données, impression, préférences, i18n).
- `tests/`, `resources/`, `packaging/` : tests d'intégration et fichiers utilisés pour construire les paquets.
- `dev/build/` : compilation locale et exécutables de développement.
- `dev/vendor/gtk-sdk/` : SDK GTK/SQLite local utilisé lorsque les paquets de développement ne sont pas installés sur le système.
- `docs/`, `PLAN.md` : documentation fonctionnelle et technique ; `.github/` contient l'intégration continue.

## Fonctions

- Catalogue avec références, unités, catégories, dates de création/modification, points de commande, stock de sécurité, plafonds, coûts et volumes.
- Entrepôts, capacités et emplacements de rangement.
- Délais fournisseurs décomposés en administration, préparation, transport et contrôle, additionnés pour les propositions de réapprovisionnement.
- Entrées, sorties PEPS/FIFO, corrections et bons de mouvement détaillant les quantités prélevées par lot.
- Lots avec date de réception, péremption et état qualité. Seuls les lots conformes et non échus sont disponibles pour une sortie.
- Alertes de rupture, surstock, péremption et suggestions JIT tenant compte des commandes déjà en cours.
- Commandes fournisseurs et clients, réservations, expéditions et transferts de lots entre entrepôts.
- Clients, tarifs négociés par article et documents de vente : proforma, bon de livraison, vente comptoir, facture et retour client. Les sorties de stock, retours en quarantaine et facturation depuis un bon de livraison sont gérées automatiquement.
- Classement ABC selon la valeur annuelle consommée, PEPS/FIFO, typologies d'articles et politiques de flux poussés, tirés, tendus et synchrones.
- Horizon de couverture configurable par article, estimation du lot économique de Wilson quand les données sont disponibles, et repères de coûts de commande, possession et pénurie.
- Inventaires, journaux, bons de réception et propositions de réapprovisionnement imprimables en format adapté au nombre de colonnes; export CSV pour Excel.
- Valorisation au prix moyen pondéré (PMP), stock dormant, fréquence des mouvements, rotation et couverture sur 90 jours, avec exports imprimables.
- Étiquettes A4 avec codes-barres Code 128, nom, référence et prix; impression à l'unité ou par planche.
- Listes principales paginées (10 à 500 lignes), recherche par référence/code-barres/désignation et saisie par lecteur code-barres USB.
- Interface claire ou sombre, cinq couleurs d'accent, palette entièrement personnalisable, taille de texte, affichage compact et quatre langues : français, anglais, espagnol et allemand.
- Réglages persistants pour l'entreprise, les documents, la numérotation, les alertes de péremption et de stock dormant, les étiquettes, la page d'accueil et la mémorisation du dernier utilisateur.
- Profils administrateur et profils de travail à permissions configurables; sauvegarde et restauration locale.

## Vérifier l'application

La suite d'intégration couvre les comptes et permissions, le catalogue, les mouvements PEPS/FIFO, les contrôles qualité, les volumes d'entrepôt, les commandes partielles, le réapprovisionnement, les sauvegardes et les documents CSV/HTML.

```sh
ctest --test-dir dev/build --output-on-failure
```

Le workflow de compilation exécute également ces tests sous Linux et Windows.

## Construire sous Linux

Prérequis : CMake 3.22+, un compilateur C++20, GTK 4 et SQLite 3 (versions de développement). Si ces paquets ne sont pas installés, utilisez le SDK local fourni dans `dev/vendor/gtk-sdk` avec les variables ci-dessous.

```sh
export SDK="$PWD/dev/vendor/gtk-sdk"
export LD_LIBRARY_PATH="$SDK/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$SDK"
export PKG_CONFIG_PATH="$SDK/usr/lib/x86_64-linux-gnu/pkgconfig:$SDK/usr/share/pkgconfig"
export PATH="$SDK/usr/bin:$PATH"
```

Construire et tester :

```sh
cmake -S . -B dev/build -DCMAKE_BUILD_TYPE=Release
cmake --build dev/build --parallel
ctest --test-dir dev/build --output-on-failure
```

L'exécutable de développement est `dev/build/gestion-stock`. Il utilise les bibliothèques GTK 4 et SQLite fournies par Linux; aucune connexion internet n'est utilisée à l'exécution. Pour l'installer dans les répertoires système :

```sh
sudo cmake --install dev/build
```

## Construire sous Windows

Installez [MSYS2](https://www.msys2.org/), ouvrez le terminal **UCRT64**, puis installez les dépendances :

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-adwaita-icon-theme mingw-w64-ucrt-x86_64-sqlite3
```

À la racine du dépôt :

```sh
cmake -S . -B dev/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build dev/build --parallel
```

L'exécutable est `dev/build/gestion-stock.exe`. Le workflow `.github/workflows/build.yml` réunit les DLL GTK et ressources de thème dans un artefact Windows; il produit également un artefact Linux. L'environnement de travail utilisé pour cette version étant Linux, seul le binaire Linux a été compilé et lancé localement.

## Première utilisation

Au premier lancement, créez le compte administrateur avec un mot de passe d'au moins 8 caractères. L'administrateur peut ensuite créer des comptes de travail et régler leurs droits séparément. Les mots de passe sont dérivés avec PBKDF2-SHA-256 et un sel individuel; ils ne sont pas stockés en clair.

Les articles commencent sans stock. Enregistrez une réception, renseignez le lot et l'emplacement, puis confirmez le contrôle qualité depuis **Rapports et méthodes**. Les lots « à contrôler », bloqués ou périmés restent visibles dans l'inventaire mais sont exclus des sorties FIFO.

Le bouton ⚙ de la barre supérieure ouvre les **Réglages** classés par thème : général, apparence et couleurs, documents et étiquettes, ventes et stock, entreprise, données et sauvegarde. Les préférences sont stockées dans la base locale et s'appliquent immédiatement.

Les données se trouvent dans le répertoire de données local de l'utilisateur. Les CSV et documents imprimables sont enregistrés dans **Documents/Gestion des stocks/Exports**; un document imprimable s'ouvre localement dans le lecteur par défaut pour lancer l'impression.

Voir [PLAN.md](PLAN.md) pour l'architecture et l'état de réalisation.

Les hypothèses, formules et notions reprises des supports de gestion sont détaillées dans [docs/Principes-et-sources.md](docs/Principes-et-sources.md).
