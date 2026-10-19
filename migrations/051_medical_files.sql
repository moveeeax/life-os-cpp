-- Migration 051: the family medical archive and the private file store.
--
-- medical_*: the people of the family, their doctors and clinics, visits,
-- records and medications, owner-scoped like every module table.
--
-- private_files: an attachment of a medical record or a travel document. The
-- bytes live in the bucket under <prefix>private/<owner>/<id>/, never under a
-- prefix the public base URL serves; the row is created pending with the
-- presigned PUT and turns ready once the object is there at the declared size.
-- The parent is polymorphic (parent_kind, parent_id), so there is no foreign
-- key: a trigger on each parent table drops its files.
--
-- private_file_purges: object keys whose row is gone. Any DELETE of a
-- private_files row (an API delete, a parent delete, an account delete, the
-- sweep of stale pending rows) leaves its key here through a trigger, in the
-- same transaction; the files_sweep job removes the objects and the entries.
--
-- Expand-only: new tables and triggers; the previous binary never reads them,
-- and the trigger on travel_documents only deletes rows of the new table.
--
-- NOTE: no BEGIN/COMMIT — MigrationRunner wraps the file in one transaction.

CREATE TABLE IF NOT EXISTS medical_people (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    name        text        NOT NULL CHECK (length(name) BETWEEN 1 AND 80),
    relation    text        NOT NULL DEFAULT '' CHECK (length(relation) <= 40),
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 1000),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS medical_people_owner ON medical_people (owner_id, name);

CREATE TABLE IF NOT EXISTS medical_providers (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    kind        text        NOT NULL CHECK (kind IN ('doctor', 'clinic')),
    name        text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    specialty   text        NOT NULL DEFAULT '' CHECK (length(specialty) <= 80),
    clinic      text        NOT NULL DEFAULT '' CHECK (length(clinic) <= 120),
    phone       text        NOT NULL DEFAULT '' CHECK (length(phone) <= 40),
    email       text        NOT NULL DEFAULT '' CHECK (length(email) <= 254),
    address     text        NOT NULL DEFAULT '' CHECK (length(address) <= 300),
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 1000),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS medical_providers_owner ON medical_providers (owner_id, name);

CREATE TABLE IF NOT EXISTS medical_visits (
    id                 uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id           uuid        NOT NULL,
    person_id          uuid        NOT NULL REFERENCES medical_people (id) ON DELETE CASCADE,
    provider_id        uuid        REFERENCES medical_providers (id) ON DELETE SET NULL,
    date               date        NOT NULL,
    reason             text        NOT NULL CHECK (length(reason) BETWEEN 1 AND 200),
    outcome            text        NOT NULL DEFAULT '' CHECK (length(outcome) <= 2000),
    follow_up          date,
    follow_up_task_id  uuid        REFERENCES task_items (id) ON DELETE SET NULL,
    created_at         timestamptz NOT NULL DEFAULT now(),
    updated_at         timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS medical_visits_owner_date ON medical_visits (owner_id, date DESC);
CREATE INDEX IF NOT EXISTS medical_visits_person ON medical_visits (person_id);
CREATE INDEX IF NOT EXISTS medical_visits_provider ON medical_visits (provider_id) WHERE provider_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS medical_visits_task ON medical_visits (follow_up_task_id) WHERE follow_up_task_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS medical_records (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    person_id   uuid        NOT NULL REFERENCES medical_people (id) ON DELETE CASCADE,
    visit_id    uuid        REFERENCES medical_visits (id) ON DELETE SET NULL,
    date        date        NOT NULL,
    kind        text        NOT NULL
                            CHECK (kind IN ('lab', 'imaging', 'prescription', 'discharge', 'vaccination',
                                            'certificate', 'other')),
    title       text        NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS medical_records_owner_date ON medical_records (owner_id, date DESC);
CREATE INDEX IF NOT EXISTS medical_records_person ON medical_records (person_id);
CREATE INDEX IF NOT EXISTS medical_records_visit ON medical_records (visit_id) WHERE visit_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS medical_medications (
    id           uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id     uuid        NOT NULL,
    person_id    uuid        NOT NULL REFERENCES medical_people (id) ON DELETE CASCADE,
    name         text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    dose         text        NOT NULL DEFAULT '' CHECK (length(dose) <= 80),
    schedule     text        NOT NULL DEFAULT '' CHECK (length(schedule) <= 200),
    start_date   date,
    end_date     date,
    end_task_id  uuid        REFERENCES task_items (id) ON DELETE SET NULL,
    note         text        NOT NULL DEFAULT '' CHECK (length(note) <= 1000),
    created_at   timestamptz NOT NULL DEFAULT now(),
    updated_at   timestamptz NOT NULL DEFAULT now(),
    CHECK (end_date IS NULL OR start_date IS NULL OR end_date >= start_date)
);
CREATE INDEX IF NOT EXISTS medical_medications_owner ON medical_medications (owner_id, end_date);
CREATE INDEX IF NOT EXISTS medical_medications_person ON medical_medications (person_id);
CREATE INDEX IF NOT EXISTS medical_medications_task ON medical_medications (end_task_id) WHERE end_task_id IS NOT NULL;

DROP TRIGGER IF EXISTS medical_people_touch_updated_at ON medical_people;
CREATE TRIGGER medical_people_touch_updated_at
    BEFORE UPDATE ON medical_people
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS medical_providers_touch_updated_at ON medical_providers;
CREATE TRIGGER medical_providers_touch_updated_at
    BEFORE UPDATE ON medical_providers
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS medical_visits_touch_updated_at ON medical_visits;
CREATE TRIGGER medical_visits_touch_updated_at
    BEFORE UPDATE ON medical_visits
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS medical_records_touch_updated_at ON medical_records;
CREATE TRIGGER medical_records_touch_updated_at
    BEFORE UPDATE ON medical_records
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS medical_medications_touch_updated_at ON medical_medications;
CREATE TRIGGER medical_medications_touch_updated_at
    BEFORE UPDATE ON medical_medications
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- ── private files ─────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS private_files (
    id            uuid        PRIMARY KEY,
    owner_id      uuid        NOT NULL,
    parent_kind   text        NOT NULL CHECK (parent_kind IN ('medical_record', 'travel_document')),
    parent_id     uuid        NOT NULL,
    name          text        NOT NULL CHECK (length(name) BETWEEN 1 AND 200),
    content_type  text        NOT NULL CHECK (length(content_type) BETWEEN 1 AND 100),
    bytes         bigint      NOT NULL CHECK (bytes > 0),
    object_key    text        NOT NULL UNIQUE CHECK (length(object_key) BETWEEN 1 AND 500),
    status        text        NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'ready')),
    created_at    timestamptz NOT NULL DEFAULT now(),
    ready_at      timestamptz,
    CHECK ((status = 'ready') = (ready_at IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS private_files_parent ON private_files (owner_id, parent_kind, parent_id);
CREATE INDEX IF NOT EXISTS private_files_pending ON private_files (created_at) WHERE status = 'pending';

CREATE TABLE IF NOT EXISTS private_file_purges (
    object_key  text        PRIMARY KEY,
    created_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS private_file_purges_created ON private_file_purges (created_at);

CREATE OR REPLACE FUNCTION private_files_purge() RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO private_file_purges (object_key) VALUES (OLD.object_key)
        ON CONFLICT (object_key) DO NOTHING;
    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS private_files_purge_object ON private_files;
CREATE TRIGGER private_files_purge_object
    AFTER DELETE ON private_files
    FOR EACH ROW EXECUTE FUNCTION private_files_purge();

-- TG_ARGV[0] is the parent_kind of the table the trigger sits on.
CREATE OR REPLACE FUNCTION private_files_drop_with_parent() RETURNS TRIGGER AS $$
BEGIN
    DELETE FROM private_files
     WHERE owner_id = OLD.owner_id AND parent_kind = TG_ARGV[0] AND parent_id = OLD.id;
    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS medical_records_drop_files ON medical_records;
CREATE TRIGGER medical_records_drop_files
    AFTER DELETE ON medical_records
    FOR EACH ROW EXECUTE FUNCTION private_files_drop_with_parent('medical_record');

DROP TRIGGER IF EXISTS travel_documents_drop_files ON travel_documents;
CREATE TRIGGER travel_documents_drop_files
    AFTER DELETE ON travel_documents
    FOR EACH ROW EXECUTE FUNCTION private_files_drop_with_parent('travel_document');
