-- scripts/player_controller.lua
--
-- Per-entity script component: bounces the entity off world-space edges.
-- Replaces the hardcoded C-side system_movement() for script-driven entities.

local PlayerController = {}

function PlayerController:on_init()
    self.speed_x = 3.0
    self.speed_y = 2.0
    self.bound = 5.0
    engine.log("[PlayerController] ready on entity " .. self.entity)
end

function PlayerController:on_fixed_update(dt)
    local x, y, sx, sy = engine.get_transform(self.entity)

    x = x + self.speed_x * dt
    y = y + self.speed_y * dt

    -- Bounce off world-space edges.
    local hw, hh = sx * 0.5, sy * 0.5

    if x + hw > self.bound then
        x = self.bound - hw
        self.speed_x = -self.speed_x
    elseif x - hw < -self.bound then
        x = -self.bound + hw
        self.speed_x = -self.speed_x
    end

    if y + hh > self.bound then
        y = self.bound - hh
        self.speed_y = -self.speed_y
    elseif y - hh < -self.bound then
        y = -self.bound + hh
        self.speed_y = -self.speed_y
    end

    engine.set_transform(self.entity, x, y, sx, sy)
end

function PlayerController:on_destroy()
    engine.log("[PlayerController] detached from entity " .. self.entity)
end

return PlayerController
