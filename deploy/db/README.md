# life_os database in the shared CNPG cluster

The `postgresql` cluster in namespace `db` is shared: it already hosts the
roles `tarassov-me` and `tgw` and the database `tgw_archive`. So the role is
added with an append patch to the array, not by replacing
`spec.managed.roles`, and after the patch the full role list is checked.

Steps:

```bash
# 1. Secret with the role password (type basic-auth, like the existing ones).
kubectl -n db create secret generic postgresql-life-os \
  --type=kubernetes.io/basic-auth \
  --from-literal=username=life-os \
  --from-literal=password="$(openssl rand -base64 32 | tr -d '/+=' | head -c 32)"

# 2. Role: op=add to the end of the array, replaces nothing.
kubectl -n db patch cluster postgresql --type=json -p='[{"op":"add","path":"/spec/managed/roles/-","value":{
  "name":"life-os","comment":"life-os-cpp db user","ensure":"present","login":true,
  "superuser":false,"createdb":false,"createrole":false,"inherit":false,"replication":false,
  "bypassrls":false,"connectionLimit":-1,"passwordSecret":{"name":"postgresql-life-os"}}}]'

# 3. Check: all roles must be present in the output.
kubectl -n db get cluster postgresql -o jsonpath='{range .spec.managed.roles[*]}{.name}{"\n"}{end}'

# 4. Database, declaratively.
kubectl apply -f deploy/db/database.yaml
kubectl -n db get database life-os -o custom-columns='NAME:.metadata.name,APPLIED:.status.applied,MSG:.status.message'

# 5. Connect as the role, not as superuser: ownership is checked by creating
#    and dropping a table.
kubectl -n db run psql-check --rm -i --restart=Never \
  --image=ghcr.io/cloudnative-pg/postgresql:18.3-system-trixie \
  --env=PGPASSWORD="$(kubectl -n db get secret postgresql-life-os -o jsonpath='{.data.password}' | base64 -d)" \
  -- psql -h postgresql-rw -U life-os -d life_os \
  -c 'select current_user, current_database();' \
  -c 'create table probe(x int); drop table probe;'
```

`databaseReclaimPolicy: retain` keeps the database alive when the `Database`
resource is deleted: health data must not vanish together with the manifest.

Application address: `postgresql-rw.db.svc.cluster.local:5432`, reads from
the replica `postgresql-ro.db.svc.cluster.local:5432`. Redis is in the same
place: `redis.db.svc.cluster.local:6379`.
