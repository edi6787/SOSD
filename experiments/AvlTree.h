#pragma once

// ======= AvlTree.h =======
// 4단계 비교 실험용 AVL 트리 (키 → 위치)
//
// 모든 노드에서 왼쪽·오른쪽 서브트리의 높이 차이가 1 이하가 되도록 회전으로 균형을 맞춥니다.
// 높이가 O(log n)으로 유지되어 삽입·검색이 O(log n)입니다.
// 같은 키가 다시 들어오면 처음 위치를 유지합니다 (lower bound와 같은 "첫 위치" 기준).

#include <algorithm>
#include <cstdint>

class AvlTree
{
public:
    AvlTree() : root(nullptr), count(0) {}
    ~AvlTree() { destroy(root); }

    AvlTree(const AvlTree&) = delete;
    AvlTree& operator=(const AvlTree&) = delete;

    void insert(uint32_t key, uint64_t position)
    {
        root = insert(root, key, position);
    }

    // 키가 있으면 true와 함께 위치를 돌려줍니다.
    bool find(uint32_t key, uint64_t& position) const
    {
        const Node* node = root;
        while (node != nullptr)
        {
            if (key < node->key)
            {
                node = node->left;
            }
            else if (key > node->key)
            {
                node = node->right;
            }
            else
            {
                position = node->position;
                return true;
            }
        }
        return false;
    }

    uint64_t size() const { return count; }
    int treeHeight() const { return height(root); }

private:
    struct Node
    {
        uint32_t key;
        int32_t height;      // 이 노드를 루트로 하는 서브트리의 높이 (잎 = 1)
        uint64_t position;
        Node* left;
        Node* right;
    };

    // ======= 높이·회전 =======

    static int height(const Node* node) { return node ? node->height : 0; }

    static void updateHeight(Node* node)
    {
        node->height = 1 + std::max(height(node->left), height(node->right));
    }

    //     node              child
    //    /    \            /     \
    //  child   C   →     A       node
    //  /   \                    /    \
    // A     B                  B      C
    static Node* rotateRight(Node* node)
    {
        Node* child = node->left;
        node->left = child->right;
        child->right = node;
        updateHeight(node);
        updateHeight(child);
        return child;
    }

    // rotateRight의 좌우 대칭
    static Node* rotateLeft(Node* node)
    {
        Node* child = node->right;
        node->right = child->left;
        child->left = node;
        updateHeight(node);
        updateHeight(child);
        return child;
    }

    // 높이 차이가 2가 된 노드의 균형을 맞춥니다 (LL, LR, RR, RL 네 경우).
    static Node* rebalance(Node* node)
    {
        updateHeight(node);
        int diff = height(node->left) - height(node->right);

        if (diff > 1)   // 왼쪽이 높음
        {
            if (height(node->left->left) < height(node->left->right))
            {
                node->left = rotateLeft(node->left);     // LR → LL
            }
            return rotateRight(node);                    // LL
        }
        if (diff < -1)  // 오른쪽이 높음
        {
            if (height(node->right->right) < height(node->right->left))
            {
                node->right = rotateRight(node->right);  // RL → RR
            }
            return rotateLeft(node);                     // RR
        }
        return node;
    }

    // ======= 삽입·삭제 =======

    // 재귀 깊이는 트리 높이(약 1.44 log2 n, 2억 개여도 40 이하)라 스택 걱정이 없습니다.
    Node* insert(Node* node, uint32_t key, uint64_t position)
    {
        if (node == nullptr)
        {
            ++count;
            return new Node{ key, 1, position, nullptr, nullptr };
        }

        if (key < node->key)
        {
            node->left = insert(node->left, key, position);
        }
        else if (key > node->key)
        {
            node->right = insert(node->right, key, position);
        }
        else
        {
            return node;   // 같은 키: 처음 위치 유지
        }
        return rebalance(node);
    }

    static void destroy(Node* node)
    {
        if (node == nullptr) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    Node* root;
    uint64_t count;
};
