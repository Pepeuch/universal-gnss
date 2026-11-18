# Universal GNSS — Pin Configurator / Web Flasher (integration)

Cette archive contient une version **patchée** du `flasher.html` fourni et un configurateur multi-cartes indépendant.

## Intégration au dépôt UG

1. Copier `flasher.html` à la racine du répertoire qui héberge déjà votre Web Flasher (faire une sauvegarde de l'ancien).
2. Copier le dossier `flasher/pinout/` et `flasher/assets/S3_ethpinout.svg` au même niveau relatif.
3. **Conserver** les autres fichiers du dépôt : `flasher/app.js`, `flasher/portable.js`, `flasher/provision.js`, manifestes firmware, logo `UG_logo.png`, etc. **Ils ne sont pas inclus** dans cette archive, et sont toujours requis pour les fonctions de flash existantes.
4. Servir la page en HTTP(S) ou sur `http://localhost` ; `file://` ne permet pas de charger le SVG avec `fetch()` de façon fiable.

Exemple de test local, dans le répertoire contenant `flasher.html` :

```bash
python3 -m http.server 8000
# puis http://localhost:8000/flasher.html
```

## Ce qui fonctionne avec le SVG S3 fourni

- La section Pin Configurator se trouve au-dessus du Firmware Flasher, et prend toute la largeur.
- Bus disponibles selon le profil : UART0/1/2, SPI2 (réservé W5500), SPI3, I2C0/1, GPIO/PPS.
- Affectation sur le SVG `.bpad` / `.prow` par `data-slot`, affichage de la fonction en couleur sur la ligne, légende et liste des affectations.
- Conflits GPIO exclusifs refusés ; broches USB/PSRAM bloquées ; broches sensibles demandent confirmation ; GPIO46 interdit pour un signal de sortie.
- Profil en `localStorage` par carte, export/import `ug-pin-config-v1`, validation des fichiers importés.
- Pont partiel vers les sélecteurs existants `portable-tx` et `portable-rx` **uniquement** lorsque l'UART GNSS a ses deux broches configurées, que les options sont autorisées par `portable.js` et que la sortie runtime correspond.
- API JavaScript : `UGPinConfigurator.getProfile()` et événement DOM `ug:pin-profile-changed`.

## C3, C6 et futures cartes

Le répertoire `flasher/pinout/boards.js` contient trois entrées :

| ID | Carte | SVG / GPIO | Firmware dans le flasher original |
| --- | --- | --- | --- |
| `s3-gateway` | Waveshare ESP32-S3-ETH | SVG S3 livré, validation active | Oui |
| `c3-rover-usb` | ESP32-C3 SuperMini | **En attente du SVG et de la validation du brochage** | Oui, via `device=c3-rover-usb` |
| `c6-future` | ESP32-C6 (référence à préciser) | **En attente du modèle, du SVG et du brochage** | **Non** : section flash masquée pour cette carte |

Pour ajouter une carte :

1. Dessiner son SVG et placer les groupes `.bpad` avec `data-slot="L1"` et `data-gpio="17"` ; et les groupes `.prow` associés avec `data-slot="L1"` (ou adapter les sélecteurs dans le configurateur).
2. Placer le SVG dans `flasher/assets/` et le déclarer dans `boards.js` via `svg: 'flasher/assets/...'`.
3. Renseigner `pins`, `locked`, `warnings`, `inputOnly`, `buses`, `gnssUarts` après vérification sur la carte réelle. Ce sont les listes **de cette carte**, pas un inventaire théorique du microcontrôleur.
4. N'ajouter `firmwareDevice` que lorsque le flasher est capable de proposer **réellement** la bonne image. Si `null`, la section flash n'est pas interactive.

## Limites importantes

- **Aucun firmware n'est modifié** par le configurateur. Il ne configure ni le W5500 ni un périphérique matériel tout seul.
- Les autres bus (SPI, I²C, PPS...) restent **dans le JSON matériel** ; le firmware UG, `portable.js` et son protocole USB doivent être étendus pour accepter ces nouveaux champs avant une application NVS automatique.
- L'ancien import `portable-import` n'est **pas** interchangeable avec l'import `ug-pin-import` : les schémas sont différents. Importer un profil matériel dans cette nouvelle section récupère la configuration du configurateur et transmet **uniquement RX/TX lorsque possible** aux champs existants.
- La fiche du S3 intègre les réservations tirées du SVG fourni ; vérifier le matériel exact et les fonctions fixées avant de raccorder les périphériques. Le support I²C partagé et le partage des lignes SPI avec plusieurs périphériques nécessiteront un modèle de bus/CS plus riche si voulu. Dans cette première version, un GPIO = un signal affecté.
- La compatibilité fonctionnelle complète avec `portable.js`, `provision.js`, `app.js` ne peut être certifiée sans ces trois fichiers. L'intégration conserve leurs balises et leurs ID, sans réécrire leur logique.

## Fichiers nouveaux

```text
flasher.html
flasher/
  assets/
    S3_ethpinout.svg
  pinout/
    boards.js
    configurator.css
    configurator.js
```
