# Principes de gestion des stocks et mise en œuvre

Ce document relie les notions des deux supports demandés aux écrans et calculs de **Gestion des stocks et logistique**. Les sources servent de base métier; les choix de calcul particuliers de l'application sont explicitement indiqués comme tels.

## Sources consultées

- [Chapitre II — Gestion des stocks, Université Frères Mentouri Constantine](https://fac.umc.edu.dz/ista/pdf/cours/Chapitre%20II%20gestion%20stock.pdf)
- [Gestion des Stocks Logistiques, document Scribd fourni](https://www.scribd.com/document/843130106/Gestion-de-Stocks)
- Pour la conception des parcours de lecture code-barres et de stock projeté : [documentation Odoo sur les codes-barres](https://www.odoo.com/documentation/18.0/applications/inventory_and_mrp/barcode/setup/software.html) et [rapport ERPNext sur le niveau de stock](https://docs.frappe.io/erpnext/stock-level-report).

## Notions et application

| Notion des supports | Mise en œuvre |
| --- | --- |
| Maintenir la production et les ventes entre deux livraisons; amortir les retards, variations et livraisons non conformes | Stocks minimum et de sécurité par article, contrôle qualité par lot, échéances et alertes de rupture. Les lots bloqués, à contrôler ou échus ne sont pas distribués. |
| Décider quand et combien commander; couvrir administration, transmission, préparation fournisseur, livraison et contrôle | Le fournisseur porte un délai détaillé en quatre étapes (administration/transmission, préparation, transport/livraison, contrôle/mise à disposition). Leur somme alimente le délai total; un délai spécifique article peut le remplacer. Le point d'alerte tient compte du délai et du stock de sécurité. |
| Stock physique, stock net, stock minimum, maximum, de sécurité et d'alerte | L'article affiche le stock conforme disponible, les réservations client, les réceptions attendues, le stock projeté et les seuils configurés. Les dépassements de plafond sont signalés. |
| Types de stocks : courant/cyclique, sécurité, tampon, anticipation, saisonnier, transit, dormant, projet, usure, transbordement et récupération; articles de production et MRO | Typologies et politiques de flux sélectionnables sur chaque fiche article. Les notes de méthode expliquent leur rôle; les catégories restent descriptives afin de ne pas appliquer des règles automatiques non validées par l'entreprise. |
| Stockage interne, entreposage, amont, flux internes et aval | L'aide intégrée distingue stockage et entreposage; les entrepôts, emplacements et transferts consignent les lieux et les mouvements internes. Les commandes fournisseurs et clients couvrent les flux amont et aval. |
| Flux poussés, tirés, tendus/JIT et synchrones | La fiche article mémorise la politique choisie. La demande des propositions est mesurée sur les sorties des 90 derniers jours; chaque article définit son horizon de couverture après délai. Le flux synchrone suppose toujours une coordination de production côté entreprise. |
| PEPS/FIFO, en particulier pour les produits périssables | Les sorties prélèvent les lots les plus anciens, conformes et non échus de l'entrepôt sélectionné. Les transferts préservent code de lot, péremption, coût et qualité. |
| Classement ABC par valeur annuelle consommée et cumul valeur / nombre d'articles | L'analyse classe toutes les références actives selon les sorties valorisées des 365 derniers jours; elle affiche la part de valeur cumulée et la part cumulée des références. Les repères A jusqu'à environ 60 %, A+B jusqu'à environ 90 % sont présentés comme usuels, pas comme des seuils universels. |
| Coûts d'achat, passation, possession et pénurie; compromis entre surstock et rupture | Les coûts unitaires, coût fixe de commande, taux annuel de possession, exposition de rupture, capacité d'entrepôt et budget estimé apparaissent dans les écrans autorisés et dans le rapport. Le coût unitaire saisi sur chaque commande fournisseur peut intégrer le prix négocié ou une remise de quantité. |
| Taille de lot économique et équilibre commande/possession | À titre d'aide complémentaire, l'application estime le lot de Wilson si elle dispose d'une consommation récente, d'un coût par commande, d'un prix unitaire et d'un taux de possession. Cette estimation n'écrase pas la quantité conseillée, qui respecte les engagements, les seuils, le délai, le plafond et la capacité. |
| Traçabilité, dates, qualité et pièces de gestion | Chaque mouvement porte horodatage, article, entrepôt, emplacement, lot, motif, référence et opérateur. Les bons clients/fournisseurs, feuilles d'inventaire, journaux, échéances, propositions et fichiers CSV sont générés localement. |

## Règles de calcul à connaître

- **Stock projeté** = stock conforme disponible + quantité restante des commandes fournisseurs ouvertes − reliquat des commandes clients ouvertes.
- **Point d'alerte** = max(stock minimum, stock de sécurité + demande quotidienne moyenne × délai fournisseur).
- **Stock cible** = max(point d'alerte, stock de sécurité + demande moyenne × (délai + horizon de couverture configuré)). Le plafond d'article et l'espace disponible peuvent réduire la quantité proposée; l'application signale alors la contrainte.
- **ABC** utilise les sorties réelles valorisées. En l'absence de consommation valorisée, les références sont en C; les pourcentages cumulés restent à zéro.
- **Lot de Wilson** : √(2 × demande annuelle × coût de commande ÷ coût annuel de possession d'une unité). Il suppose des coûts et une demande stables et doit être interprété comme un repère.

Le stock de sécurité et l'horizon sont saisis sur l'article par l'administrateur ou un profil auquel il accorde la gestion des articles. Les composantes du délai fournisseur sont des durées estimées; le suivi des dates réelles d'envoi et de réception permet à l'équipe de comparer ensuite les délais observés.
