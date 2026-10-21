# Test report — Gestion des stocks et logistique

**Date:** 2026-10-20  
**Build tested:** local Release build, GTK 4.22.4, SQLite 3.46.1  
**Test database:** `/tmp/opencode/e2e-home/.local/share/GestionStock/gestion-stock.sqlite3`  
**Automation:** AT-SPI accessibility tree plus injected keyboard input  
**Automated suite:** `165 vérifications, 0 échec(s)` (`ctest` passed)

## 1. Scope and method

The application was tested through a complete business flow on an isolated profile, not with a pre-filled demonstration database. The scenario covered first-run setup, two user profiles, products, a supplier, customers and negotiated prices, warehouses, receipts, FIFO issues, transfers, corrections, a write-off, a manual recovery, partial and complete purchase orders, customer orders, every sales-document type, returns, quality control, reports, exports, settings, backup, persistence and permission restrictions.

The report distinguishes confirmed defects from observations that may be intentional. All monetary and stock checks were reconciled against the SQLite database.

## 2. Result summary

- No crash, data loss or transaction corruption was observed.
- FIFO selection, blocked-lot exclusion, document totals, partial receipts, customer-order shipping, transfers, returns and stock valuation reconciled correctly.
- Two calculation-feedback defects affect operational data entry:
  - a purchase-order cost stays at the previous article's cost when the article is changed;
  - a positive stock correction can create a lot at zero cost when no article was initially selected.
- Spanish and German interfaces are incomplete, despite the product presenting four languages.
- Backup creation works; restore could not be completed because the file chooser was not exposed in the automated environment.
- Native tooltip popups exist in the GTK implementation, but their visual hover rendering could not be inspected because pointer injection is frozen in this XWayland session.

## 3. Detailed test log

### 3.1 First run, login and users

| Test | Result | Evidence |
| --- | --- | --- |
| Empty setup form rejected | Pass | Required-field validation shown. |
| Short password rejected | Pass | `Utilisez un mot de passe valide d'au moins 8 caractères.` |
| Password confirmation mismatch rejected | Pass | `Les mots de passe ne correspondent pas.` |
| Administrator created and logged in | Pass | `testeur` / `Testeur E2E`. |
| Wrong password rejected | Pass | `Identifiant ou mot de passe incorrect.` |
| Work profile created | Pass | `vendeur`, non-admin, selected permissions. |
| Permission changes saved | Pass | Dialog checkboxes persisted after reopening. |
| Account deactivated and reactivated | Pass | Confirmation shown; state persisted. |
| Password reset with invalid password rejected | Pass | Same 8-character validation. |
| Password reset with valid password | Pass | Login with reset password succeeded. |
| Users page hidden from work profile | Pass | Navigation item absent. |

### 3.2 Products, supplier, customers and warehouses

| Test | Result | Evidence |
| --- | --- | --- |
| Product creation, search, edit, archive | Pass | `TEST-001` created then archived; active catalogue remained at 3 products. |
| Customer-specific sale price | Pass | MAT-001 price changed from 140.00 to the negotiated price for Boulangerie E2E. |
| Supplier lead-time sum | Pass | 1 + 2 + 2 + 1 = 6 days; stored and displayed correctly. |
| Supplier edit | Pass | Contact updated; lead time remained 6 days. |
| Customer creation and edit | Pass | C-001 and C-002 created; address, contact and payment terms saved. |
| Warehouse creation | Pass | `Dépôt E2E` (120.5 m³) and `Annexe E2E` (initially 50 m³). |
| Warehouse capacity below occupied volume rejected | Pass | `La capacité ne peut pas être inférieure au volume déjà occupé.` |
| Warehouse capacity edit | Pass | Annexe E2E changed to 60 m³ and persisted. |
| Read-only restrictions for work profile | Pass | No add/edit buttons for articles, warehouses or movements. |
| Supplier management permission | Pass | Work profile could add/edit suppliers as configured. |

### 3.3 Stock movements

| Test | Result | Evidence |
| --- | --- | --- |
| Manual receipt and quality state | Pass | Lots created with cost, location, expiry and `À contrôler` default. |
| Quality control | Pass | Return lot changed to `CONFORME` with control note. |
| FIFO issue excludes non-conforming lots | Pass | EMB invoice consumed `L-EMB-002` while 120 units of `L-EMB-001` remained blocked. |
| FIFO across lots | Pass | MAT-001 consumed 5 from `L-FAR-002`, leaving 10. |
| Transfer between warehouses | Pass | 5 MAT-002 moved Dépôt → Annexe E2E; lot code, cost and quality preserved. |
| Positive correction | Partly pass | EMB correction at 12.00 created `LOT-22`; MAT-002 correction created `LOT-20` at 0.00 (defect D2). |
| Negative correction | Pass | MAT-002 reduced 23 → 22 with reason stored. |
| Write-off | Pass | `LOT-20` reduced 1 → 0 with `WRITE_OFF` movement. |
| Manual customer return / recovery | Pass | 1 EMB at 12.00 created `LOT-24`, `CONFORME`, location `RECUP-E2E`. |
| Barcode/reference scan | Pass | `MAT-001` selected the correct article. |
| Unknown code scan | Pass | `Aucun article ne correspond à ce code-barres ou à cette référence.` |
| Movement history and print | Pass | Types, quantities, costs, references, reasons and operator rendered. |

### 3.4 Purchase orders

| Test | Result | Evidence |
| --- | --- | --- |
| Draft creation and send | Pass | CMD-1 changed to `ENVOYÉE`. |
| Partial receipt | Pass | 10 of 30 kg received; status `PARTIELLE`; lot `L-SUC-003`. |
| Remaining receipt | Pass | 20 kg received as `L-SUC-004`; status `REÇUE`. |
| Quantity/stock constraint | Pass | Over-receipt blocked by application rules. |
| Cost inheritance on article change | **Fail** | CMD-2 selected MAT-001 (cost 100.00) but saved estimated cost 12.00, inherited from EMB-001 (defect D1). |
| Cancel a purchase order | **Missing** | No cancel action is offered, including for drafts. |
| Draft order in projected stock | Observation | Draft CMD-2 (5 MAT-001) counted as incoming; reorder proposal fell from 9.67 to 4.67 (D5). |

### 3.5 Customer orders and sales documents

| Test | Result | Evidence |
| --- | --- | --- |
| Customer order creation and partial shipment | Pass | CL-000001 shipped 5 then 5; status `LIVRÉE`. |
| Customer-order reservation release | Pass | CL-000002 cancelled after confirmation; reserved quantity released. |
| Invoice with negotiated price and discounts | Pass | FAC-20261020-0001: subtotal 825.00, discount 41.25, tax 148.9125, total 932.6625. |
| BL creation and stock issue | Pass | BL-20261020-0002: 2 MAT-002, subtotal 240.00, tax 45.60, total 285.60. |
| BL → invoice conversion | Pass | FAC-20261020-0003 links to source document 2; no duplicate stock movement. |
| Proforma | Pass | PRO-20261020-0004 created without stock impact. |
| Return and expiry validation | Pass | Missing expiry rejected for perishable article; after expiry entered, RET-20261020-0005 created quarantined lot. |
| Return quality control | Pass | Returned lot became `CONFORME`; sale stock value included the 2 returned units. |
| Counter sale | Partly pass | CPT-20261020-0006 saved with subtotal 50.00, discount 1.25, tax 9.2625, total 58.0125. |
| Counter sale without customer | **Fail** | `— Aucun client —` is selectable but saving fails with `Indiquez le nom du client (200 caractères maximum).` (D3). |
| Insufficient conforming stock | Pass | Invoice rejected with available quantity; no partial document was retained. |
| Automatic HTML document generation | Pass | Documents written under `/tmp/Gestion des stocks/Exports`. |

### 3.6 Reports, inventory and exports

| Test | Result | Evidence |
| --- | --- | --- |
| PMP stock valuation | Pass | Current value 15,380.00 DA; reconciled unit by unit, including 82.00 lot cost and returned stock. |
| ABC analysis | Pass | Consumption values and cumulative shares matched the sales history. |
| FIFO / rotation / coverage | Pass | Recomputed from movement and sales dates. |
| Dormant stock | Pass | No false positive; value and last-out fields consistent. |
| Reorder proposals | Partly pass | Quantities follow demand, lead time, safety and capacity, but include draft purchase orders (D5). |
| Printable reports | Pass | Inventory, journal, goods receipt, recommendations, dormant stock, labels and sales documents generated. |
| Inventory CSV | Pass | 9 active lots exported with escaped commas/quotes and correct quantities/costs. |
| Recommendations print for work profile | Pass | Cost and financial columns omitted. |
| Dead-stock value for work profile | Observation | `Valeur immobilisée` is displayed even when `canViewCosts` is false (D7). |

### 3.7 Settings, persistence and backup

| Test | Result | Evidence |
| --- | --- | --- |
| Theme, accent, compact mode, font scale | Pass | Applied immediately and persisted; restored to dark/teal/100/off at end. |
| Company information in documents | Partly pass | Name, address, phone, tax ID and footer rendered; email was never rendered (D6). |
| Reset settings — cancel | Pass | No value changed. |
| Reset settings — confirm | Pass | Preferences and company fields reset; business data, users and documents remained intact. |
| Backup creation | Pass | `sauvegarde-gestion-stock-2026-10-20_13-39-59.sqlite3`; `PRAGMA integrity_check` returned `ok`. |
| Backup restore | **Blocked** | The restore action did not expose a usable file chooser in the automation environment. |
| Restart persistence | Pass | After restart: 6 sales documents, 2 customer orders, 12 lots, 23 movements (24 after manual recovery), 3 active products. |
| Exports location | Observation | In this isolated profile, exports were written under `/tmp/Gestion des stocks/Exports`, not under a persistent documents directory. |

### 3.8 Languages and tooltips

| Test | Result | Evidence |
| --- | --- | --- |
| FR → EN → ES → DE → FR | Partly pass | Language changes apply, but ES/DE screens contain English and French fallbacks (D4). |
| Tooltip presence | Pass | Hints are attached to navigation, buttons, fields, panels, metrics and dialogs. |
| Tooltip localization | Fail | Tooltips remain French in all four languages (D4). |
| Native hover rendering | Blocked | Pointer injection is frozen; no native tooltip popup screenshot could be produced. |
| Accessibility side effect | Observation | For text fields, the tooltip becomes the accessible name instead of the field label (D8). |

## 4. Defects and observations

### D1 — Purchase-order estimated cost does not refresh when the article changes (high)

**Reproduction**

1. Open `Commandes fournisseurs → Nouvelle commande`.
2. Leave the default article or select EMB-001 (cost 12.00), then change the article to MAT-001 (cost 100.00).
3. Save.

**Actual:** CMD-2 is stored with `unit_cost = 12.00` for MAT-001.  
**Expected:** The estimated cost should follow the selected article or be explicitly entered per order without inheriting another article's cost.  
**Cause:** The cost field is initialized once at `src/app.cpp:1312`; unlike sales lines (`src/app.cpp:1533`), no `notify::selected` handler updates it.

### D2 — Positive correction can create a zero-cost lot (high)

**Reproduction**

1. Open `Mouvements → Nouveau mouvement`.
2. Keep the unselected article, choose `Correction positive`, enter quantity 1, then select MAT-002.
3. Do not manually change the cost; save.

**Actual:** `LOT-20` is created at `unit_cost = 0.00` even though MAT-002 costs 80.00–82.00.  
**Expected:** Changing the article should refresh the default receipt cost, or saving a zero cost for a valuation-bearing movement should require confirmation.  
**Cause:** The cost field is initialized to 0 when no article is selected (`src/app.cpp:1209`) and has no article-change handler.

### D3 — Counter sale cannot use “no customer” (medium)

`— Aucun client —` is a valid dropdown choice, but saving fails because `createSalesDocument` requires a non-empty customer name (`src/database.cpp:1035`). This contradicts the UI and blocks anonymous walk-in sales. Either allow a walk-in label for counter sales or remove the empty choice.

### D4 — Spanish and German are incomplete (medium)

The dictionaries contain approximately 435 ES and 433 DE keys versus 895 EN keys. Confirmed fallbacks include `Sales & customers` in ES/DE and `Comportement de l'application au quotidien.` in non-French screens. Tooltips are hardcoded French. The README's claim of four fully available languages is therefore misleading.

### D5 — Draft purchase orders affect projected stock; orders cannot be cancelled (medium / confirm intent)

CMD-2 in `BROUILLON` reduced the MAT-001 reorder proposal from 9.67 to 4.67. If draft orders are not commitments, they should not be counted as incoming. Separately, no purchase-order cancel action exists at any status, so an erroneous draft such as CMD-2 cannot be withdrawn.

### D6 — Company email is a dead setting (low)

`company.email` is stored and reloaded (`src/preferences.cpp:110,152`) but is not rendered by generated documents (`src/reports.cpp`). Either print it with the other company details or remove the field.

### D7 — Dead-stock value bypasses the cost permission (low)

The dormant-stock panel always appends `Valeur immobilisée : …` (`src/app.cpp:1982`), even for a work profile without `canViewCosts`. The current amount is 0.00 DA, but the value will become visible once dormant stock exists. The printable version is also unrestricted.

### D8 — Tooltip replaces text-field accessible names (low / accessibility)

`applyTooltips` sets tooltip text on entries; AT-SPI then exposes the tooltip as the accessible name, e.g. `« Scanner un code-barres / saisir une référence » : champ de saisie…` instead of the visible field label. Keep the field label as the accessible name and expose the hint as a description.

### D9 — Minor consistency observations

- Quality values mix localized defaults (`À contrôler`) with uppercase canonical values (`CONFORME`, `BLOQUÉ`).
- When a second warehouse exists, forms default to it (Annexe E2E) rather than the main Dépôt E2E.
- The 19% tax on a 140.00 line yields 26.60; 5% discount arithmetic was correct throughout, including 932.6625 and 316.54 totals.

## 5. Calculation spot checks

- Invoice FAC-20261020-0001: `5 × 25 = 125`, `5 × 140 = 700`, gross 825; 5% discount 41.25; net 783.75; 19% tax 148.9125; total 932.6625. **Correct.**
- BL/FAC Épicerie: `2 × 120 = 240`; tax 45.60; total 285.60. Conversion kept the same totals and did not move stock again. **Correct.**
- Return RET-20261020-0005: `2 × 140 = 280`; 5% discount 14.00; net 266.00; tax 50.54; total 316.54. **Correct.**
- Counter sale: `2 × 25 = 50`; 5% discount 1.25; net 48.75; tax 9.2625; total 58.0125. **Correct.**
- FIFO: blocked EMB lot untouched; 5 units taken from conforming `L-EMB-002`; MAT-001 taken from `L-FAR-002`. **Correct.**
- Transfer: total on-hand unchanged; destination lot retained cost 80.00 and code `L-SUC-001`. **Correct.**
- PMP: 60×100 + 22×80 + 10×100 + 20×82 + 120×12 + 43×12 + 10×80 + 20×80 + 2×100 + 5×80 + 1×12 + 1×12 = 15,380.00 DA. **Matches application.**

## 6. Coverage gaps

- Native tooltip hover popup and visual screenshots (pointer injection unavailable).
- Complete backup restore flow (no usable file chooser under automation).
- Windows build/run (Linux-only environment; CI covers it).
- ASAN verification (not rebuilt in the cleaned workspace).
- Individual changes of startup page, last-user memory, date format, currency, paper size, orientation, signature toggle and alert thresholds. Theme, accent, compact mode, font scale and reset were exercised.
- Physical printer output; HTML generation was checked instead.

## 7. Artifacts

- Database: `/tmp/opencode/e2e-home/.local/share/GestionStock/gestion-stock.sqlite3`
- Exports and backups: `/tmp/Gestion des stocks/Exports`
- Backup integrity: `ok`
- Automated test result: `165 vérifications, 0 échec(s)`
- Current stock value: 15,380.00 DA across 12 active lots

## 8. Recommended priority

1. Fix D1 and D2 before any operational use: both silently write incorrect costs into lots and purchase orders.
2. Decide D3 and D5 behavior; both affect daily sales/reordering.
3. Complete ES/DE translations or narrow the language claim.
4. Fix the permission leak D7 and the dead email field D6.
5. Verify restore manually with a normal desktop session, then test a restored database.

## 9. Addendum — corrections verified on 2026-10-21

The following changes were made after the initial report.

- **Admin account self-service:** an administrator can now change their own username, display name and password from **Utilisateurs → Modifier mon compte**. The database method rejects non-admin actors, duplicate usernames and invalid passwords; an empty password preserves the current one. The audit log and remembered username are updated.
- **D1 fixed:** the purchase-order unit cost now follows the selected article.
- **D2 fixed:** the movement receipt cost now follows the selected article, including positive corrections.
- **D3 fixed:** a counter sale without an identified customer is stored as **Client de passage**; invoices still require a customer.
- **D6 fixed:** the company email is rendered in generated document headers.
- **D7 fixed:** the dead-stock value text, column and printed value are hidden from profiles without cost permission.
- **D8 fixed:** field labels are used as accessible names and concise hints as descriptions; static labels no longer expose redundant tooltips.
- **Tooltips:** only the hovered control is described, without repeating `Information affichée` or the visible label.
- **D5 fixed:** draft purchase orders no longer count as incoming stock. A purchase order that has not received stock can be cancelled from **Réapprovisionnement**; cancelled orders are removed from projected stock and supplier suggestions.
- **Translations:** EN/ES/DE strings were added for the new admin-account screen. ES/DE coverage remains incomplete overall (D4).
- **Tests:** the automated suite now reports `180 vérifications, 0 échec(s)` and `ctest` passes. The admin username/password change was also exercised through the UI and then restored to the documented E2E administrator account.

Remaining from the initial report: D4 (incomplete ES/DE coverage) and D9 (minor consistency observations). Visual hover rendering, restore flow, Windows and ASAN checks remain unverified for the reasons listed in section 6.
