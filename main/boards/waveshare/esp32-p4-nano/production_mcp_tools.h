#pragma once

// Registers the production-control tools exposed to the device's MCP client.
// This function is idempotent.
void InitializeProductionMcpTools();
