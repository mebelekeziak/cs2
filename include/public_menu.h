#pragma once

// Call on the render owner thread, with the overlay's ImGui context current.
void ConfigurePublicMenuStyle();
// Call between ImGui::NewFrame() and ImGui::Render().
void DrawPublicMenu();
