# Premier essai S3 alpha — 15 novembre 2026

## Périmètre et référence

Waveshare ESP32-S3 Ethernet W5500, puce S3 QFN56 révision 0.2, Flash 16 Mio,
PSRAM octale 8 Mio ; USB Serial/JTAG et Ethernet local. Aucun récepteur GNSS
raccordé. Hôte Linux 7.0.0-43-generic, outils ESP-IDF 6.1 épinglés par
`migration/dependencies.json`, esptool 5.3.1. Le C3 et le C6 sont restés intacts.

Image corrigée réellement installée : arbre Git
`53cdeffcef013cc169a88ba5ecd98eb58e6c29a1`, SHA256 de l’application
`7d9e0edb6d693a69d974bda75e6ba6d154ce95ec771f5a761778897c880f8ddc`.
Cet arbre de travail immuable est une preuve locale, pas un commit publié ni une
source officiellement promue dans `main`. Les deux reconstructions indépendantes
ont produit les mêmes quatre segments.

## Protection et corrections

- Identification de la puce et des caractéristiques avant écriture ; Secure Boot
  et chiffrement Flash désactivés sur cette carte.
- Sauvegarde intégrale des 16 Mio hors Git avec permissions privées ; comparaison
  du digest contre la Flash par la ROM. Le stub rapide échouait en lecture ; la
  lecture ROM complète et sa vérification ont réussi.
- Table de partitions identique à l’existant. Quatre segments initiaux aux offsets
  `0`, `0x8000`, `0xf000`, `0x20000`, vérifiés après écriture. NVS identique octet
  pour octet avant le premier démarrage. Aucun effacement global.
- Le firmware initial du commit `03e0ba2961bb1ce36543c6148c9047ab88e19e30`
  débordait la pile principale de 8 Kio. Les profils réseau utilisent maintenant
  32 Kio pour cette pile de démarrage, libérée au retour de `app_main` ; le Rover
  conserve 8 Kio. La marge minimale mesurée avec configuration persistante et
  identité TLS est de **19 304 octets**.
- Le premier accès HTTPS plaçait la RAM interne sous les seuils d’admission,
  donnant des réponses 503. Le S3 utilise maintenant la PSRAM pour mbedTLS et
  préfère celle-ci pour les allocations générales dépassant 1 Kio. Les piles,
  allocations DMA et réserves explicitement internes restent soumises au SDK.
  Les seuils du contrôleur de ressources sont inchangés.
- Une tentative de contrôle de démarrage a été lancée trop tôt pendant une
  écriture corrective. Celle-ci a échoué ; une nouvelle écriture exclusive,
  suivie d’une vérification intégrale des quatre segments, a réussi. Les accès
  série suivants ont été sérialisés. La dernière correction mémoire n’a réécrit
  que l’application, les trois autres segments étant identiques.

La sauvegarde originale, les identifiants, l’adresse réseau, les journaux bruts et
l’identité d’administration/TLS restent privés. La procédure ne provisionne
aucun eFuse et n’efface pas les données pour obtenir un test vert.

## Résultats observés

| Contrôle | Résultat et portée |
|---|---|
| Boot/PSRAM | PASS après correction ; test PSRAM SDK réussi, retour de `app_main`, aucune nouvelle boucle observée pendant les essais courts |
| W5500/Ethernet | PASS initialisation et obtention DHCP ; accès HTTPS réel depuis l’hôte |
| Console USB | PASS commandes de statut, historique et provisionnement local |
| HTTPS | PASS identité locale vérifiée par empreinte/certificat ; administration anonyme refusée |
| API | PASS statut, capacités, ressources, configuration authentifiée et aperçu portable |
| Restauration portable USB | PASS export, validation, application des mêmes valeurs et relecture identique, sans reset automatique |
| WebUI Chromium 141 | PASS authentification, réglages GNSS visibles sans récepteur, caster activé/désactivé, déconnexion |
| Association ESP-NOW | PASS du rejet d’une activation sans pair/clé valides ; aucune association fictive créée |
| GNSS/UART physique | NON TESTABLE : aucun récepteur raccordé ; GNSS laissé désactivé |
| ESP-NOW/RTCM reçu au Rover | NON TESTABLE : aucun Rover reflashé/appairé pour ce firmware chiffré |
| NTRIP externe/RTK FIX | NON TESTABLE : pas de source configurée ni de récepteur ; la réponse des services n’est pas une preuve de réception |
| LoRa/MQTT | NON DISPONIBLE : fonctions non implémentées dans cet alpha |

La restauration portable par console USB est vérifiée séparément des opérations
Web Serial : ne pas confondre un test logiciel du navigateur avec un flash ou une
restauration physique effectués depuis le navigateur.

Après les accès HTTPS, un échantillon de ressources indiquait 70 655 octets libres
internes et un plus grand bloc de 34 816 octets : pression **réduite**, mutations
Web autorisées, diagnostics détaillés des satellites suspendus. Le correctif
évite le blocage critique observé ; il ne qualifie pas le budget RAM avec toutes
les fonctions radio/GNSS simultanément actives.

## Limites et invalidation

Cette campagne courte ne qualifie ni endurance, portée RF, câblage UART, livraison
RTCM au Rover, OTA, ni matériel d’une autre carte. Un changement de carte,
partitionnement, SDK, configuration mémoire ou code propriétaire invalide les
résultats correspondants et demande une validation ciblée. Le relais C3 ne dispose
que de 1 344 octets de marge dans son slot applicatif lors du build de ce lot ;
sa compilation n’est pas une qualification de son fonctionnement matériel.

Le site public et les nouveaux workflows restent à qualifier après intégration
et déclenchement explicitement décidés par le mainteneur. Voir
[la procédure alpha](../../docs/flasher/ALPHA.md).
