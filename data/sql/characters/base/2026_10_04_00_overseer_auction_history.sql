-- overseer_auction_history: one row per auction that ends, sold or expired.
--
-- The market engine prices from observed sale history. The core keeps none:
-- log_money records only sales of 500 gold or more, the auction log channel is
-- off, and auction mail is kept 30 days for our own characters only. The core
-- calls OnAuctionSuccessful and OnAuctionExpire as an auction closes
-- (AuctionHouseObject::Update); OverseerAuctionScript writes one row for each.
--
-- outcome   'sold' (a bidder or buyout won it) or 'expired' (no bidder).
-- house     the core's AuctionHouseId: 2 alliance, 6 horde, 7 neutral.
-- bid       the winning bid. A buyout sets it to the buyout price, so for a
--           sold row it is the price paid. For an expired row it is the
--           starting bid and is not a price anyone paid.
-- buyout    the listed buyout price, 0 when there was none.
-- price_paid  what the buyer paid: bid when sold, 0 when expired.
-- seller_kind, buyer_kind
--           'random_bot', 'guild_member', 'family', 'player' or 'unknown'.
--           An expired auction has no buyer, which reads 'unknown'.
-- No character names: the guid is the character's low guid.
CREATE TABLE IF NOT EXISTS `overseer_auction_history` (
    `id`           BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
    `occurred_at`  TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `house`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `item_entry`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `item_count`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `bid`          INT UNSIGNED     NOT NULL DEFAULT 0,
    `buyout`       INT UNSIGNED     NOT NULL DEFAULT 0,
    `price_paid`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `outcome`      ENUM('sold','expired') NOT NULL,
    `seller_guid`  INT UNSIGNED     NOT NULL DEFAULT 0,
    `seller_kind`  VARCHAR(16)      NOT NULL DEFAULT 'unknown',
    `buyer_guid`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `buyer_kind`   VARCHAR(16)      NOT NULL DEFAULT 'unknown',
    PRIMARY KEY (`id`),
    KEY `idx_item_time` (`item_entry`, `occurred_at`),
    KEY `idx_time` (`occurred_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
