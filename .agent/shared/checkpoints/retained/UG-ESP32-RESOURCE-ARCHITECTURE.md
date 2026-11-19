# Audit de capacité du Gateway S3

Lifecycle: RETAINED
Status: IMPLEMENTED (audit documentaire uniquement)
Scope: DOCUMENTATION / VALIDATION

Référence source `952eca0e65651f7f187c372a9dfe4fa80131c092`. Le [rapport et le protocole de qualification](../../../../docs/analysis/ESP32_ARCHITECTURE_RESOURCE_AUDIT.md) portent les chiffres, les sources et les hypothèses. [Plan ESP](../../../../TODO.md).

- Sept workers réseau créés d'après les capacités de carte. Leurs piles réservent 59 392 octets. Les callbacks de sortie sont publiés de façon asynchrone ; leur retrait doit être acquitté avant libération du contexte.
- Le boot physique du 19 novembre et les empreintes des artefacts n'identifient pas à eux seuls un ELF exact pour une trace. Conserver le couple BIN/ELF et les états configuré, connecté et en flux dans le prochain A/B.
- Reprendre à la mesure appariée au démarrage et aux transitions, puis comparer création à la demande et I/O coopérative avec connexion lente. Les mesures avant/après sur la refonte et la charge maximale restent à faire. Conserver les seuils et l'indépendance GNSS/radio/console. Le relevé sans flux ne clôt aucun checkpoint de qualification radio ou NTRIP.
- Revoir ces conclusions si changent les sites de création, leur affinité, les tailles de types, le profil ou le SDK. Aucun changement de firmware dans cette étude.
