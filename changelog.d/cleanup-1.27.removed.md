Unused code and config: the Kafka messaging subsystem (`src/messaging`,
librdkafka, the `messaging.*` config keys, the compose Kafka/Zookeeper/exporter
services, the cpp-env Kafka template, `make up-kafka`), `Webhooks::send`, the
`note` part of `GET /api/v1/today` (`PUT`/`DELETE /api/v1/today/note` stay),
the empty `src/billing/Billing.hpp`, `Repositories::safe_order_by` and the
unread `billing.provider` key.
