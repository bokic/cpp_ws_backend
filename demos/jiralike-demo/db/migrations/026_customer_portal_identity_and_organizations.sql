-- Migration 026: Dedicated Portal-Only Customer Identity, Customer Organizations, and Comment Privacy

-- 1. Differentiate user categories in users ('INTERNAL', 'PORTAL_CUSTOMER', 'SERVICE_ACCOUNT')
ALTER TABLE users ADD COLUMN user_type TEXT NOT NULL DEFAULT 'INTERNAL';

-- 2. Customer Organizations for grouping customers and sharing tickets
CREATE TABLE IF NOT EXISTS customer_organizations (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    service_desk_id INTEGER NOT NULL REFERENCES service_desks(id) ON DELETE CASCADE,
    name            TEXT NOT NULL,
    domain          TEXT DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS customer_organization_members (
    organization_id INTEGER NOT NULL REFERENCES customer_organizations(id) ON DELETE CASCADE,
    user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    added_at        TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (organization_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_customer_org_desk ON customer_organizations(service_desk_id);
CREATE INDEX IF NOT EXISTS idx_customer_org_member_user ON customer_organization_members(user_id);

-- 3. Work item association with customer organizations
ALTER TABLE work_items ADD COLUMN customer_organization_id INTEGER DEFAULT NULL REFERENCES customer_organizations(id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS idx_work_items_cust_org ON work_items(customer_organization_id);

-- 4. Internal vs Public comment flag (0 = public / visible to customer, 1 = internal worknote / agent only)
ALTER TABLE comments ADD COLUMN is_internal INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS idx_comments_is_internal ON comments(work_item_id, is_internal);
