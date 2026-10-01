# База life_os в общем кластере CNPG

Кластер `postgresql` в namespace `db` общий: в нём уже живут роли
`tarassov-me` и `tgw` и база `tgw_archive`. Поэтому роль добавляется
патчем-добавлением в массив, а не заменой `spec.managed.roles`, и после патча
состав ролей проверяется целиком.

Порядок:

```bash
# 1. Секрет с паролем роли (тип basic-auth, как у существующих).
kubectl -n db create secret generic postgresql-life-os \
  --type=kubernetes.io/basic-auth \
  --from-literal=username=life-os \
  --from-literal=password="$(openssl rand -base64 32 | tr -d '/+=' | head -c 32)"

# 2. Роль: op=add в конец массива, ничего не заменяет.
kubectl -n db patch cluster postgresql --type=json -p='[{"op":"add","path":"/spec/managed/roles/-","value":{
  "name":"life-os","comment":"life-os-cpp db user","ensure":"present","login":true,
  "superuser":false,"createdb":false,"createrole":false,"inherit":false,"replication":false,
  "bypassrls":false,"connectionLimit":-1,"passwordSecret":{"name":"postgresql-life-os"}}}]'

# 3. Проверка: в выводе обязаны быть все роли.
kubectl -n db get cluster postgresql -o jsonpath='{range .spec.managed.roles[*]}{.name}{"\n"}{end}'

# 4. База декларативно.
kubectl apply -f deploy/db/database.yaml
kubectl -n db get database life-os -o custom-columns='NAME:.metadata.name,APPLIED:.status.applied,MSG:.status.message'

# 5. Подключение ролью, а не суперпользователем: владение проверяется
#    созданием и удалением таблицы.
kubectl -n db run psql-check --rm -i --restart=Never \
  --image=ghcr.io/cloudnative-pg/postgresql:18.3-system-trixie \
  --env=PGPASSWORD="$(kubectl -n db get secret postgresql-life-os -o jsonpath='{.data.password}' | base64 -d)" \
  -- psql -h postgresql-rw -U life-os -d life_os \
  -c 'select current_user, current_database();' \
  -c 'create table probe(x int); drop table probe;'
```

`databaseReclaimPolicy: retain` оставляет базу живой при удалении ресурса
`Database`: данные здоровья не должны исчезать вместе с манифестом.

Адрес для приложения: `postgresql-rw.db.svc.cluster.local:5432`, чтение с
реплики `postgresql-ro.db.svc.cluster.local:5432`. Redis там же:
`redis.db.svc.cluster.local:6379`.
