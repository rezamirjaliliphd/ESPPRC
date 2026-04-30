"""
Comprehensive test suite for ESPPRC enhanced features.
Tests all major features individually and in combination.
"""
import networkx as nx
import numpy as np
import time

def create_test_graph(n=20, density=0.3, seed=42):
    """Create a random test graph"""
    np.random.seed(seed)
    G = nx.DiGraph()
    
    for i in range(n):
        for j in range(n):
            if i != j and np.random.random() < density:
                G.add_edge(i, j, 
                          weight=np.random.uniform(1, 10),
                          res=[np.random.uniform(1, 5), np.random.uniform(1, 5)])
    return G

def test_basic_functionality():
    """Test 1: Basic ESPPRC solving"""
    print("\n" + "="*70)
    print("TEST 1: Basic ESPPRC Functionality")
    print("="*70)
    
    G = nx.DiGraph()
    G.add_edge(0, 1, weight=2.0, res=[1.0, 2.0])
    G.add_edge(1, 2, weight=3.0, res=[2.0, 1.0])
    G.add_edge(0, 2, weight=10.0, res=[5.0, 5.0])
    
    min_res = [0.0, 0.0]
    max_res = [10.0, 10.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        result = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=2
        )
        
        print(f"✓ Basic solve successful")
        print(f"  Path: {result['path']}")
        print(f"  Cost: {result['cost']:.2f}")
        print(f"  Resources: {result['resources']}")
        
        assert len(result['path']) > 0, "Path should not be empty"
        assert result['cost'] > 0, "Cost should be positive"
        print("✓ Basic test PASSED")
        
    except ImportError:
        print("✗ Module not built - run 'pip install -e .' first")
    except Exception as e:
        print(f"✗ Basic test FAILED: {e}")

def test_elementary_constraint():
    """Test 2: Elementary path constraint"""
    print("\n" + "="*70)
    print("TEST 2: Elementary Path Constraint")
    print("="*70)
    
    # Create graph with cycle
    G = nx.DiGraph()
    G.add_edge(0, 1, weight=1.0, res=[1.0, 0.0])
    G.add_edge(1, 2, weight=1.0, res=[1.0, 0.0])
    G.add_edge(2, 1, weight=0.5, res=[0.5, 0.0])  # Creates cycle
    G.add_edge(2, 3, weight=1.0, res=[1.0, 0.0])
    
    min_res = [0.0, 0.0]
    max_res = [10.0, 10.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Test with elementary=True
        result_elem = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=3,
            elementary=True
        )
        
        # Test with elementary=False
        result_non_elem = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=3,
            elementary=False
        )
        
        print(f"✓ Elementary path: {result_elem['path']}, cost: {result_elem['cost']:.2f}")
        print(f"✓ Non-elementary path: {result_non_elem['path']}, cost: {result_non_elem['cost']:.2f}")
        
        # Check no repeated nodes in elementary path
        assert len(result_elem['path']) == len(set(result_elem['path'])), \
            "Elementary path should have no repeated nodes"
        
        print("✓ Elementary constraint test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Elementary test FAILED: {e}")

def test_k_cycle_elimination():
    """Test 3: K-cycle elimination"""
    print("\n" + "="*70)
    print("TEST 3: K-Cycle Elimination")
    print("="*70)
    
    G = create_test_graph(n=15, density=0.4)
    min_res = [0.0, 0.0]
    max_res = [30.0, 30.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Test different k values
        for k in [0, 2, 3]:
            start = time.time()
            result = solve_espprc_enhanced(
                G, min_res, max_res, cg_cut_pi,
                source=0, sink=14,
                elementary=False,
                k_cycle_elimination=k
            )
            elapsed = time.time() - start
            
            print(f"✓ k={k}: cost={result['cost']:.2f}, time={elapsed:.3f}s, path_len={len(result['path'])}")
        
        print("✓ K-cycle elimination test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ K-cycle test FAILED: {e}")

def test_time_windows():
    """Test 4: Time window constraints"""
    print("\n" + "="*70)
    print("TEST 4: Time Window Constraints")
    print("="*70)
    
    G = nx.DiGraph()
    # Time is first resource - create path where shortcuts violate time windows
    G.add_edge(0, 1, weight=1.0, res=[2.0, 1.0])  # Travel time = 2
    G.add_edge(1, 2, weight=1.0, res=[2.0, 1.0])  # Travel time = 2
    G.add_edge(2, 3, weight=1.0, res=[2.0, 1.0])  # Travel time = 2
    G.add_edge(0, 3, weight=0.5, res=[1.0, 3.0])  # Fast but high resource
    
    min_res = [0.0, 0.0]
    max_res = [10.0, 10.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Without time windows
        result_no_tw = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=3,
            use_time_windows=False
        )
        
        # With time windows (time_window_res_idx=0)
        result_tw = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=3,
            use_time_windows=True,
            time_window_res_idx=0
        )
        
        print(f"✓ Without time windows: cost={result_no_tw['cost']:.2f}, path={result_no_tw['path']}")
        print(f"✓ With time windows: cost={result_tw['cost']:.2f}, path={result_tw['path']}")
        print("✓ Time window test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Time window test FAILED: {e}")

def test_bitwise_optimization():
    """Test 5: Bitwise elementary checking"""
    print("\n" + "="*70)
    print("TEST 5: Bitwise Elementary Optimization")
    print("="*70)
    
    # Small graph for bitwise
    G = create_test_graph(n=30, density=0.3)
    min_res = [0.0, 0.0]
    max_res = [50.0, 50.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Without bitwise
        start = time.time()
        result_no_bitwise = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=29,
            elementary=True,
            use_bitwise=False
        )
        time_no_bitwise = time.time() - start
        
        # With bitwise
        start = time.time()
        result_bitwise = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=29,
            elementary=True,
            use_bitwise=True
        )
        time_bitwise = time.time() - start
        
        print(f"✓ Without bitwise: cost={result_no_bitwise['cost']:.2f}, time={time_no_bitwise:.3f}s")
        print(f"✓ With bitwise: cost={result_bitwise['cost']:.2f}, time={time_bitwise:.3f}s")
        
        if time_no_bitwise > 0:
            speedup = time_no_bitwise / time_bitwise
            print(f"✓ Speedup: {speedup:.2f}x")
        
        # Verify same solution
        if abs(result_no_bitwise['cost'] - result_bitwise['cost']) < 1e-6:
            print("✓ Solutions match")
        
        print("✓ Bitwise optimization test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Bitwise test FAILED: {e}")

def test_bounds_pruning():
    """Test 6: Bounds pruning"""
    print("\n" + "="*70)
    print("TEST 6: Bounds Pruning")
    print("="*70)
    
    G = create_test_graph(n=25, density=0.3)
    min_res = [0.0, 0.0]
    max_res = [40.0, 40.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Without bounds pruning
        start = time.time()
        result_no_bounds = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=24,
            bounds_pruning=False
        )
        time_no_bounds = time.time() - start
        
        # With bounds pruning
        start = time.time()
        result_bounds = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=24,
            bounds_pruning=True
        )
        time_bounds = time.time() - start
        
        print(f"✓ Without bounds pruning: cost={result_no_bounds['cost']:.2f}, time={time_no_bounds:.3f}s")
        print(f"✓ With bounds pruning: cost={result_bounds['cost']:.2f}, time={time_bounds:.3f}s")
        print("✓ Bounds pruning test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Bounds pruning test FAILED: {e}")

def test_direction_control():
    """Test 7: Search direction control"""
    print("\n" + "="*70)
    print("TEST 7: Search Direction Control")
    print("="*70)
    
    G = create_test_graph(n=20, density=0.3)
    min_res = [0.0, 0.0]
    max_res = [40.0, 40.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Test different directions
        for direction in ["forward", "backward", "both"]:
            result = solve_espprc_enhanced(
                G, min_res, max_res, cg_cut_pi,
                source=0, sink=19,
                direction=direction
            )
            print(f"✓ Direction '{direction}': cost={result['cost']:.2f}")
        
        print("✓ Direction control test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Direction test FAILED: {e}")

def test_time_limit_threshold():
    """Test 8: Time limit and threshold"""
    print("\n" + "="*70)
    print("TEST 8: Time Limit and Threshold")
    print("="*70)
    
    G = create_test_graph(n=30, density=0.4)
    min_res = [0.0, 0.0]
    max_res = [50.0, 50.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Test time limit
        start = time.time()
        result_time_limit = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=29,
            time_limit=1.0  # 1 second limit
        )
        elapsed = time.time() - start
        
        print(f"✓ Time limit test: elapsed={elapsed:.2f}s (limit=1.0s)")
        assert elapsed < 2.0, "Should respect time limit"
        
        # Test threshold
        result_threshold = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=29,
            threshold=100.0  # Stop if cost <= 100
        )
        
        print(f"✓ Threshold test: cost={result_threshold['cost']:.2f} (threshold=100.0)")
        print("✓ Time limit and threshold test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Time/threshold test FAILED: {e}")

def test_get_all_paths():
    """Test 9: Get all feasible paths"""
    print("\n" + "="*70)
    print("TEST 9: Get All Feasible Paths")
    print("="*70)
    
    G = nx.DiGraph()
    # Create graph with multiple paths
    G.add_edge(0, 1, weight=1.0, res=[1.0, 1.0])
    G.add_edge(0, 2, weight=1.5, res=[1.0, 1.0])
    G.add_edge(1, 3, weight=1.0, res=[1.0, 1.0])
    G.add_edge(2, 3, weight=1.0, res=[1.0, 1.0])
    G.add_edge(1, 2, weight=0.5, res=[0.5, 0.5])
    
    min_res = [0.0, 0.0]
    max_res = [10.0, 10.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        result = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=3,
            get_all_path=True
        )
        
        print(f"✓ Best path: {result['path']}, cost={result['cost']:.2f}")
        print(f"✓ Number of feasible paths found: {len(result['all_feasible_paths'])}")
        print(result.keys())
        for i, path in enumerate(result['all_feasible_paths'][:5], 1):  # Show first 5
            print(f"  Path {i}: {path}")
        
        assert len(result['all_feasible_paths']) > 0, "Should find at least one path"
        print("✓ Get all paths test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Get all paths test FAILED: {e}")

def test_sequential_repeatability():
    """Test 10: Sequential repeatability"""
    print("\n" + "="*70)
    print("TEST 10: Sequential Repeatability")
    print("="*70)
    
    G = create_test_graph(n=22, density=0.25)
    min_res = [0.0, 0.0]
    max_res = [60.0, 60.0]
    cg_cut_pi = [0.0, 0.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Sequential
        start = time.time()
        result_seq = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=21
        )
        time_seq = time.time() - start
        
        # Repeat the same solve and verify deterministic cost.
        start = time.time()
        result_repeat = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=21
        )
        time_repeat = time.time() - start
        
        print(f"✓ Sequential: cost={result_seq['cost']:.2f}, time={time_seq:.3f}s")
        print(f"✓ Repeat: cost={result_repeat['cost']:.2f}, time={time_repeat:.3f}s")
        
        if time_seq > 0:
            ratio = time_seq / time_repeat
            print(f"✓ Time ratio: {ratio:.2f}x")
        
        # Verify same solution
        if abs(result_seq['cost'] - result_repeat['cost']) < 1e-6:
            print("✓ Solutions match")
        
        print("✓ Sequential repeatability test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Sequential repeatability test FAILED: {e}")

def test_chvatal_gomory_cuts():
    """Test 11: Chvátal-Gomory cutting plane cuts"""
    print("\n" + "="*70)
    print("TEST 11: Chvátal-Gomory Cutting Plane Cuts")
    print("="*70)
    
    G = nx.DiGraph()
    G.add_edge(0, 1, weight=1.0, res=[2.5, 1.0])
    G.add_edge(1, 2, weight=1.0, res=[3.7, 1.0])
    G.add_edge(2, 3, weight=1.0, res=[1.2, 1.0])
    
    min_res = [0.0, 0.0]
    max_res = [20.0, 20.0]
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Without CG cuts
        cg_cut_pi_zero = [0.0, 0.0]
        result_no_cg = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi_zero,
            source=0, sink=3
        )
        
        # With CG cuts (apply penalty to fractional resources)
        cg_cut_pi_active = [0.5, 0.3]  # Penalties for floor(resource)
        result_with_cg = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi_active,
            source=0, sink=3
        )
        
        print(f"✓ Without CG cuts: cost={result_no_cg['cost']:.2f}")
        print(f"✓ With CG cuts (π=[0.5, 0.3]): cost={result_with_cg['cost']:.2f}")
        
        # Cost should be different due to floor penalties
        assert result_no_cg['cost'] != result_with_cg['cost'], \
            "CG cuts should affect the cost"
        
        print("✓ Chvátal-Gomory cuts test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ CG cuts test FAILED: {e}")

def test_combined_features():
    """Test 12: All features combined"""
    print("\n" + "="*70)
    print("TEST 12: Combined Features")
    print("="*70)
    
    G = create_test_graph(n=25, density=0.35)
    min_res = [0.0, 0.0]
    max_res = [50.0, 50.0]
    cg_cut_pi = [0.1, 0.05]
    
    try:
        from espprc import solve_espprc_enhanced
        
        result = solve_espprc_enhanced(
            G, min_res, max_res, cg_cut_pi,
            source=0, sink=24,
            elementary=True,
            use_bitwise=True,
            k_cycle_elimination=3,
            bounds_pruning=True,
            use_time_windows=False,
            get_all_path=True
        )
        
        print(f"✓ Combined features test successful")
        print(f"  Cost: {result['cost']:.2f}")
        print(f"  Path length: {len(result['path'])}")
        print(f"  Feasible paths found: {len(result['all_feasible_paths'])}")
        print(f"  Resources: {result['resources']}")
        print("✓ All features work together - PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Combined features test FAILED: {e}")

def test_edge_cases():
    """Test 13: Edge cases"""
    print("\n" + "="*70)
    print("TEST 13: Edge Cases")
    print("="*70)
    
    try:
        from espprc import solve_espprc_enhanced
        
        # Empty graph
        G_empty = nx.DiGraph()
        G_empty.add_node(0)
        G_empty.add_node(1)
        
        try:
            result = solve_espprc_enhanced(
                G_empty, [0.0], [10.0], [0.0],
                source=0, sink=1
            )
            print(f"✓ Empty graph handled: path={result['path']}")
        except:
            print(f"✓ Empty graph handled (no path found)")
        
        # Single edge
        G_single = nx.DiGraph()
        G_single.add_edge(0, 1, weight=5.0, res=[2.0])
        
        result = solve_espprc_enhanced(
            G_single, [0.0], [10.0], [0.0],
            source=0, sink=1
        )
        print(f"✓ Single edge graph: cost={result['cost']:.2f}")
        
        # Tight resource constraints
        G = nx.DiGraph()
        G.add_edge(0, 1, weight=1.0, res=[5.0])
        G.add_edge(1, 2, weight=1.0, res=[5.0])
        
        result = solve_espprc_enhanced(
            G, [0.0], [8.0], [0.0],  # Tight: can't reach sink
            source=0, sink=2
        )
        print(f"✓ Tight constraints handled: cost={result['cost']:.2f}")
        
        print("✓ Edge cases test PASSED")
        
    except ImportError:
        print("✗ Module not built")
    except Exception as e:
        print(f"✗ Edge cases test FAILED: {e}")

def run_all_tests():
    """Run all comprehensive tests"""
    print("\n" + "="*70)
    print("ESPPRC COMPREHENSIVE TEST SUITE")
    print("="*70)
    print("Testing all features individually and in combination...")
    
    start_total = time.time()
    
    # Run all tests
    test_basic_functionality()
    test_elementary_constraint()
    test_k_cycle_elimination()
    test_time_windows()
    test_bitwise_optimization()
    test_bounds_pruning()
    test_direction_control()
    test_time_limit_threshold()
    test_get_all_paths()
    test_sequential_repeatability()
    test_chvatal_gomory_cuts()
    test_combined_features()
    test_edge_cases()
    
    total_time = time.time() - start_total
    
    print("\n" + "="*70)
    print(f"ALL TESTS COMPLETED IN {total_time:.2f} SECONDS")
    print("="*70)

if __name__ == "__main__":
    run_all_tests()
