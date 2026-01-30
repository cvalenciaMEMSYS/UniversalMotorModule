"""Quick test of file discovery."""
from interactive_energy_analyzer import discover_test_files

print("=== M1 Tests ===")
tests = discover_test_files('C:/Users/camiv/OneDrive - MEMSYS/Documents/joulescope/M1')
print(f'Found {len(tests)} tests')
for t in tests:
    print(f"  {t['test_info']['test_id']}: {t['status']}")

print("\n=== M2 Tests ===")
tests = discover_test_files('C:/Users/camiv/OneDrive - MEMSYS/Documents/joulescope/M2')
print(f'Found {len(tests)} tests')
for t in tests:
    print(f"  {t['test_info']['test_id']}: {t['status']}")
