# Feuille de route — Gestion des stocks et logistique

## Objectif

Construire une application de bureau C++ simple à prendre en main, disponible pour Windows et Linux, qui stocke les données localement et couvre le cycle de vie des articles, les mouvements physiques, le réapprovisionnement, la traçabilité et les documents imprimables.

## Étapes de réalisation

### 1. Socle de l’application
- Projet CMake multiplateforme, interface de bureau native en français et base SQLite locale.
- Premier démarrage guidé, création du compte administrateur, connexion, déconnexion et stockage sûr des mots de passe.
- Gestion des erreurs, transactions atomiques, horodatages et journal d’audit.

### 2. Catalogue et organisation des entrepôts
- Création et modification des articles, unités, catégories, références, lots, coûts, volumes, fournisseurs et emplacements.
- Fiches article avec dates de création/modification et règles de stock minimum/commande, sécurité et maximum.
- Entrepôts, zones et capacités pour suivre l’occupation et éviter le surstockage.

### 3. Opérations et traçabilité
- Réceptions, sorties et corrections avec utilisateur, date/heure, motif, référence et lot.
- Calcul du stock conforme disponible en temps réel; sorties PEPS/FIFO des lots les plus anciens en premier.
- Contrôle qualité par lot (à contrôler, conforme, bloqué), suivi des échéances et blocage des lots non conformes/périmés.
- Historique horodaté, bons détaillés des lots prélevés et blocage des sorties impossibles.

### 4. Alertes, coûts et approvisionnement
- Alertes de rupture et de seuil de commande, stock de sécurité et plafond maximum.
- Recommandations de quantité à commander selon la demande, le délai fournisseur et les limites de stockage.
- Indicateurs de valeur de stock, coûts de possession et occupation des entrepôts; outils pour limiter les surstocks.

### 5. Analyse et méthodes
- Classement ABC selon la valeur de consommation, avec seuils cumulés et détail des calculs.
- Aide au juste-à-temps (JIT) à partir de la consommation observée, du délai et du stock de sécurité.
- Alertes de péremption et règles FIFO pour les marchandises périssables.

### 6. Documents et échanges
- Impression des inventaires, fiches de stock, journaux d’entrées/sorties, bons de réception et propositions de commande.
- Exports CSV compatibles avec les tableurs pour les petites structures.
- Sauvegarde et restauration locale de la base.

### 7. Comptes et permissions
- Profil administrateur et profils de travail configurables.
- Droits par fonctionnalité (consultation, mouvements, impression, articles, coûts, réglages et utilisateurs).
- Garde-fous pour protéger le dernier administrateur et empêcher l’auto-révocation accidentelle.

### 8. Finition et livraison
- Vérification de compilation et de démarrage Linux; workflow de production d'artefacts Windows/Linux et documentation d'installation.
- Parcours visuel cohérent, textes d’aide clairs, confirmation des opérations sensibles et affichage lisible des dates/heures.

### 9. Ventes, analyse et personnalisation avancée
- Clients, tarifs par client, proformas, bons de livraison, ventes comptoir, factures et retours avec sorties de stock et facturation depuis livraison.
- Valorisation PMP, stock dormant, statistiques de flux, rotation et couverture, étiquettes Code 128 imprimables.
- Palette de couleurs libre, quatre langues, page d'accueil configurable, identité d'entreprise et paramètres de numérotation et d'alertes.

## État

- [x] Étape 1 — socle, authentification et stockage local
- [x] Étape 2 — catalogue, entrepôts, emplacements et capacités
- [x] Étape 3 — mouvements, lots FIFO et contrôle qualité
- [x] Étape 4 — ruptures, maximum, coûts et commandes
- [x] Étape 5 — ABC, FIFO et JIT
- [x] Étape 6 — documents imprimables, CSV, sauvegarde et restauration
- [x] Étape 7 — profils et droits par fonctionnalité
- [x] Étape 8 — finition, compilation Linux et workflow de génération Windows/Linux
- [x] Étape 9 — clients et ventes, PMP, stock dormant, flux, étiquettes et personnalisation avancée

La revue métier a ajouté les commandes clients et le stock projeté net, les transferts inter-entrepôts avec conservation des lots, les retours en attente qualité, les typologies et politiques de flux, l'horizon de couverture configurable, les coûts de pénurie et l'estimation de Wilson. Les listes sont paginées de 10 à 500 lignes selon le réglage choisi. Les rapports détaillés passent au format paysage A3 lorsque le nombre de colonnes l'exige.

La suite d'intégration est exécutée en continu sur les deux systèmes dans le workflow. Elle vérifie notamment l'ordre FIFO au coût réel des lots, les notes qualité conservées à la date du mouvement, les réservations et livraisons partielles, les calculs de seuils et de coûts, les plafonds, les capacités, la pagination et l'intégrité des exports. Voir [docs/Principes-et-sources.md](docs/Principes-et-sources.md) pour la correspondance avec les deux supports.

## Réglages et personnalisation

- Thème système, clair ou sombre, cinq accents, palette de couleurs entièrement personnalisable, taille de texte et mode compact.
- Langues française, anglaise, espagnole et allemande, format de date, devise, pagination et page d'accueil.
- Valeurs par défaut des documents : papier A4/A3 automatique, orientation, signatures, ouverture automatique, numérotation et identité de l'entreprise.
- Alertes de péremption et de stock dormant, dimensions et contenu des étiquettes, mémorisation du dernier utilisateur.
- Préférences enregistrées dans la base locale (`app_meta`) et appliquées sans redémarrage.

## Choix d’architecture

- **C++20 + GTK 4** pour une interface de bureau commune aux environnements Linux et Windows.
- **SQLite** embarqué pour les données structurées, les transactions atomiques et les sauvegardes.
- **Base par utilisateur** dans le répertoire de données de l’application; aucun serveur web ni service distant requis.
- **CSV et documents HTML imprimables locaux** afin de faciliter les échanges et l’impression sans dépendre d’un service en ligne.
